#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "api/run/RunByConfig.hpp"
#include "api/connector/ExchangeProductConnector.hpp"
#include "api/route/RoutePlan.hpp"
#include "api/market/MarketDataReactor.hpp"
#include "api/market/RedundantPublicMarketDataRoute.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalMarketDataBus.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/metrics/Metrics.hpp"
#include "primitives/composite/OrderBookDeltaRuntimeV1.hpp"
#include "primitives/composite/StreamMeta.hpp"

#include "metrics/MetricsControl.hpp"
#include "metrics/Probes.hpp"
#include "probes/TimeDelta.hpp"

namespace hftrec::capture {

namespace {

constexpr unsigned kRecorderTradesKeepaliveMs = 0u;
constexpr unsigned kRecorderQuietStreamKeepaliveMs = 5000u;
constexpr unsigned kRecorderReconnectRetryMs = 100u;
constexpr std::uint64_t kRecorderBookTickerStaleReconnectNs = 3000000000ull;

EventSequenceIds nextEventSequenceIds(std::atomic<std::uint64_t>& channelCounter,
                                      std::atomic<std::uint64_t>& ingestCounter) noexcept {
    EventSequenceIds ids{};
    ids.captureSeq = channelCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    ids.ingestSeq = ingestCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    return ids;
}

void recordCxetLatencyIfEnabled(cxet::metrics::LatencyProbe& probe,
                                TscTick startTsc,
                                bool captureMetrics) noexcept {
    if (captureMetrics) {
        probe.record(startTsc, cxet::probes::captureTsc());
    }
}

replay::TradeRow makeTradeRow(const cxet_bridge::CapturedTradeRow& trade,
                              std::string_view exchange,
                              std::string_view market,
                              const EventSequenceIds& sequenceIds) noexcept {
    replay::TradeRow row{};
    row.tradeId = trade.tradeId;
    row.firstTradeId = trade.firstTradeId;
    row.lastTradeId = trade.lastTradeId;
    row.symbol = trade.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(trade.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = trade.priceE8;
    row.qtyE8 = trade.qtyE8;
    row.quoteQtyE8 = trade.quoteQtyE8;
    row.side = trade.side;
    row.isBuyerMaker = trade.isBuyerMaker ? 1u : 0u;
    row.sideBuy = trade.sideBuy ? 1u : 0u;
    return row;
}
replay::BookTickerRow makeBookTickerRow(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                        std::string_view exchange,
                                        std::string_view market,
                                        const EventSequenceIds& sequenceIds) noexcept {
    replay::BookTickerRow row{};
    row.symbol = bookTicker.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(bookTicker.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.bidPriceE8 = bookTicker.bidPriceE8;
    row.bidQtyE8 = bookTicker.bidQtyE8;
    row.askPriceE8 = bookTicker.askPriceE8;
    row.askQtyE8 = bookTicker.askQtyE8;
    return row;
}

std::vector<replay::PricePair> makePricePairs(const std::vector<cxet_bridge::CapturedLevel>& levels) {
    std::vector<replay::PricePair> out;
    out.reserve(levels.size());
    for (const auto& level : levels) {
        out.push_back(replay::PricePair{level.priceI64, level.qtyI64, level.side});
    }
    return out;
}

std::vector<replay::PricePair> makeOrderbookLevels(const cxet_bridge::CapturedOrderBookRow& depth) {
    auto out = makePricePairs(depth.bids);
    const auto asks = makePricePairs(depth.asks);
    out.insert(out.end(), asks.begin(), asks.end());
    return out;
}

replay::DepthRow makeDepthRow(const cxet_bridge::CapturedOrderBookRow& depth) {
    replay::DepthRow row{};
    row.tsNs = static_cast<std::int64_t>(depth.tsNs);
    row.levels = makeOrderbookLevels(depth);
    return row;
}

replay::SnapshotDocument makeSnapshotDocument(const cxet_bridge::CapturedOrderBookRow& snapshot) {
    replay::SnapshotDocument document{};
    document.tsNs = static_cast<std::int64_t>(snapshot.tsNs);
    document.levels = makeOrderbookLevels(snapshot);
    return document;
}

bool sleepCaptureStopAware(const std::atomic<bool>* stopRequested, unsigned delayMs) noexcept {
    constexpr unsigned kSleepStepMs = 50u;
    unsigned sleptMs = 0u;
    while (sleptMs < delayMs) {
        if (stopRequested != nullptr && stopRequested->load(std::memory_order_acquire)) return false;
        const unsigned leftMs = delayMs - sleptMs;
        const unsigned chunkMs = leftMs > kSleepStepMs ? kSleepStepMs : leftMs;
        std::this_thread::sleep_for(std::chrono::milliseconds(chunkMs));
        sleptMs += chunkMs;
    }
    return !(stopRequested != nullptr && stopRequested->load(std::memory_order_acquire));
}

bool readTradeSnapshot(const cxet::api::market::PublicMarketDataSnapshot& snapshot,
                       cxet::composite::TradeRuntimeV1& trade,
                       cxet::composite::StreamMeta& meta) noexcept {
    for (std::uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const std::uint64_t before = snapshot.version.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const auto stream = snapshot.lastStream;
        const auto status = snapshot.lastStatus;
        const auto hasTrade = snapshot.hasTrade;
        meta = snapshot.meta;
        trade = snapshot.trade;
        const std::uint64_t after = snapshot.version.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            return stream == cxet::api::market::PublicMarketDataStream::Trades
                && status == cxet::api::market::PublicMarketDataStatus::Parsed
                && hasTrade != 0u;
        }
    }
    return false;
}

bool readBookTickerSnapshot(const cxet::api::market::PublicMarketDataSnapshot& snapshot,
                            cxet::composite::BookTickerRuntimeV1& bookTicker,
                            cxet::composite::StreamMeta& meta) noexcept {
    for (std::uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const std::uint64_t before = snapshot.version.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const auto stream = snapshot.lastStream;
        const auto status = snapshot.lastStatus;
        const auto hasBookTicker = snapshot.hasBookTicker;
        meta = snapshot.meta;
        bookTicker = snapshot.bookTicker;
        const std::uint64_t after = snapshot.version.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            return stream == cxet::api::market::PublicMarketDataStream::BookTicker
                && status == cxet::api::market::PublicMarketDataStatus::Parsed
                && hasBookTicker != 0u;
        }
    }
    return false;
}

bool readOrderbookSnapshot(const cxet::api::market::PublicMarketDataSnapshot& snapshot,
                           cxet::composite::OrderBookDeltaRuntimeV1& delta,
                           cxet::composite::StreamMeta& meta) noexcept {
    for (std::uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const std::uint64_t before = snapshot.version.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const auto stream = snapshot.lastStream;
        const auto status = snapshot.lastStatus;
        const auto hasOrderbook = snapshot.hasOrderbook;
        meta = snapshot.meta;
        delta = snapshot.orderbook;
        const std::uint64_t after = snapshot.version.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            return stream == cxet::api::market::PublicMarketDataStream::Orderbook
                && status == cxet::api::market::PublicMarketDataStatus::Parsed
                && hasOrderbook != 0u;
        }
    }
    return false;
}

bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0u; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

ExchangeId exchangeIdFromConfig(std::string_view exchange) noexcept {
    if (textEqualsAscii(exchange, "binance")) return canon::kExchangeIdBinance;
    if (textEqualsAscii(exchange, "kucoin")) return canon::kExchangeIdKucoin;
    if (textEqualsAscii(exchange, "gate")) return canon::kExchangeIdGate;
    if (textEqualsAscii(exchange, "bitget")) return canon::kExchangeIdBitget;
    return canon::kExchangeIdUnknown;
}

canon::MarketType marketTypeFromConfig(std::string_view market) noexcept {
    if (textEqualsAscii(market, "swap")) return canon::kMarketTypeSwap;
    return canon::kMarketTypeFutures;
}

const char* marketDataStatusName(cxet::api::market::PublicMarketDataStatus status) noexcept {
    switch (status) {
        case cxet::api::market::PublicMarketDataStatus::Ok: return "Ok";
        case cxet::api::market::PublicMarketDataStatus::NoFrame: return "NoFrame";
        case cxet::api::market::PublicMarketDataStatus::Parsed: return "Parsed";
        case cxet::api::market::PublicMarketDataStatus::ParseSkipped: return "ParseSkipped";
        case cxet::api::market::PublicMarketDataStatus::ParseFailed: return "ParseFailed";
        case cxet::api::market::PublicMarketDataStatus::ConnectFailed: return "ConnectFailed";
        case cxet::api::market::PublicMarketDataStatus::Disconnected: return "Disconnected";
        case cxet::api::market::PublicMarketDataStatus::Reconnected: return "Reconnected";
        case cxet::api::market::PublicMarketDataStatus::BadConfig: return "BadConfig";
        case cxet::api::market::PublicMarketDataStatus::UnsupportedRoute: return "UnsupportedRoute";
        case cxet::api::market::PublicMarketDataStatus::Stopped: return "Stopped";
    }
    return "Unknown";
}

cxet::api::market::PublicMarketDataWirePreference wirePreferenceForConfig(const CaptureConfig& config) noexcept {
    const bool sbeFutures = config.market == "futures_usd"
        && (config.exchange == "gate" || config.exchange == "bitget");
    return sbeFutures
        ? cxet::api::market::PublicMarketDataWirePreference::Sbe
        : cxet::api::market::PublicMarketDataWirePreference::Auto;
}

}  // namespace

Status CaptureCoordinator::startManagedMarketData_(const CaptureConfig& config, ManagedStreamKind stream) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;

    CaptureConfig normalizedConfig = config;
    switch (stream) {
        case ManagedStreamKind::Trades: {
            auto subscribeBuilder = internal::makeTradesBuilder(normalizedConfig);
            if (!internal::applyRequestedAliases(normalizedConfig.tradesAliases, subscribeBuilder, lastError_)) {
                return Status::InvalidArgument;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.tradesEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.tradesPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.tradesPath);
                }
                if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Trades))) {
                    lastError_ = "failed to create trades.jsonl";
                    return Status::IoError;
                }
            }
            desiredTrades_.store(true, std::memory_order_release);
            tradesRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::BookTicker: {
            for (const auto* requiredAlias : {"bidQty", "askQty"}) {
                if (std::find(normalizedConfig.bookTickerAliases.begin(), normalizedConfig.bookTickerAliases.end(), requiredAlias)
                    == normalizedConfig.bookTickerAliases.end()) {
                    normalizedConfig.bookTickerAliases.push_back(requiredAlias);
                }
            }
            auto subscribeBuilder = internal::makeBookTickerBuilder(normalizedConfig);
            if (!internal::applyRequestedAliases(normalizedConfig.bookTickerAliases, subscribeBuilder, lastError_)) {
                return Status::InvalidArgument;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.bookTickerEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.bookTickerPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.bookTickerPath);
                }
                if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::BookTicker))) {
                    lastError_ = "failed to create bookticker.jsonl";
                    return Status::IoError;
                }
            }
            desiredBookTicker_.store(true, std::memory_order_release);
            bookTickerRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::Orderbook: {
            if (normalizedConfig.orderbookAliases.empty()) {
                normalizedConfig.orderbookAliases = {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"};
            }
            for (const auto* requiredAlias : {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"}) {
                if (std::find(normalizedConfig.orderbookAliases.begin(), normalizedConfig.orderbookAliases.end(), requiredAlias)
                    == normalizedConfig.orderbookAliases.end()) {
                    normalizedConfig.orderbookAliases.push_back(requiredAlias);
                }
            }
            auto subscribeBuilder = internal::makeOrderbookSubscribeBuilder(normalizedConfig);
            if (!internal::applyRequestedAliases(normalizedConfig.orderbookAliases, subscribeBuilder, lastError_)) {
                return Status::InvalidArgument;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.orderbookEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.depthPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.depthPath);
                }
                if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::DepthDelta))) {
                    lastError_ = "failed to create depth.jsonl";
                    return Status::IoError;
                }
            }
            desiredOrderbook_.store(true, std::memory_order_release);
            orderbookRunning_.store(true, std::memory_order_release);
            break;
        }
    }

    if (marketDataThread_.joinable() && !marketDataRunning_.load(std::memory_order_acquire)) {
        marketDataThread_.join();
    }
    if (!marketDataThread_.joinable()) {
        marketDataStop_.store(false, std::memory_order_release);
        marketDataRunning_.store(true, std::memory_order_release);
        marketDataThread_ = std::thread([this, normalizedConfig]() mutable noexcept {
            marketDataManagerLoop_(normalizedConfig);
        });
    }
    return Status::Ok;
}

Status CaptureCoordinator::startTrades(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::Trades);
}

Status CaptureCoordinator::requestStopTrades() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::Trades);
    return Status::Ok;
}

Status CaptureCoordinator::stopTrades() noexcept {
    (void)requestStopTrades();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

Status CaptureCoordinator::startLiquidations(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;
    lastError_ = "liquidation capture is unavailable with the current CXET public market-data manager API";
    return Status::Unimplemented;
}

Status CaptureCoordinator::requestStopLiquidations() noexcept {
    liquidationsStop_.store(true, std::memory_order_release);
    liquidationsRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopLiquidations() noexcept {
    (void)requestStopLiquidations();
    if (liquidationsThread_.joinable()) liquidationsThread_.join();
    return Status::Ok;
}

Status CaptureCoordinator::startBookTicker(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::BookTicker);
}

Status CaptureCoordinator::requestStopBookTicker() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::BookTicker);
    return Status::Ok;
}

Status CaptureCoordinator::stopBookTicker() noexcept {
    (void)requestStopBookTicker();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

Status CaptureCoordinator::startOrderbook(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::Orderbook);
}

Status CaptureCoordinator::requestStopOrderbook() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::Orderbook);
    return Status::Ok;
}

Status CaptureCoordinator::stopOrderbook() noexcept {
    (void)requestStopOrderbook();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

void CaptureCoordinator::requestStopManagedMarketData_(ManagedStreamKind stream) noexcept {
    switch (stream) {
        case ManagedStreamKind::Trades:
            desiredTrades_.store(false, std::memory_order_release);
            tradesRunning_.store(false, std::memory_order_release);
            tradesStop_.store(true, std::memory_order_release);
            break;
        case ManagedStreamKind::BookTicker:
            desiredBookTicker_.store(false, std::memory_order_release);
            bookTickerRunning_.store(false, std::memory_order_release);
            bookTickerStop_.store(true, std::memory_order_release);
            break;
        case ManagedStreamKind::Orderbook:
            desiredOrderbook_.store(false, std::memory_order_release);
            orderbookRunning_.store(false, std::memory_order_release);
            orderbookStop_.store(true, std::memory_order_release);
            break;
    }
}

bool CaptureCoordinator::anyManagedMarketDataDesired_() const noexcept {
    return desiredTrades_.load(std::memory_order_acquire)
        || desiredBookTicker_.load(std::memory_order_acquire)
        || desiredOrderbook_.load(std::memory_order_acquire);
}

void CaptureCoordinator::joinManagedMarketDataIfIdle_() noexcept {
    if (anyManagedMarketDataDesired_()) return;
    marketDataStop_.store(true, std::memory_order_release);
    if (marketDataThread_.joinable()) marketDataThread_.join();
    marketDataRunning_.store(false, std::memory_order_release);
}


enum class DirectRouteConnectStatus {
    Ok,
    BadConfig,
    WsConnectFailed,
    PayloadSendFailed,
    PostConnectPayloadSendFailed,
};

const char* directRouteConnectStatusName(DirectRouteConnectStatus status) noexcept {
    switch (status) {
        case DirectRouteConnectStatus::Ok: return "Ok";
        case DirectRouteConnectStatus::BadConfig: return "BadConfig";
        case DirectRouteConnectStatus::WsConnectFailed: return "WsConnectFailed";
        case DirectRouteConnectStatus::PayloadSendFailed: return "PayloadSendFailed";
        case DirectRouteConnectStatus::PostConnectPayloadSendFailed: return "PostConnectPayloadSendFailed";
    }
    return "Unknown";
}

DirectRouteConnectStatus connectMarketDataRouteNoTradingSync(cxet::api::market::PublicMarketDataRoute& route) noexcept {
    if (!route.prepared || !route.route.config) return DirectRouteConnectStatus::BadConfig;
    const auto status = cxet::api::connectWsEndpointByRoutePlanDetailed(
        route.client,
        route.route,
        nullptr,
        0u,
        false);
    if (status != cxet::api::RouteConnectStatus::Ok) {
        route.connected = false;
        return DirectRouteConnectStatus::WsConnectFailed;
    }
    route.connected = true;
    if (route.route.payload && route.route.payloadLen > 0u && !route.client.send(route.route.payload, route.route.payloadLen)) {
        route.client.disconnect();
        route.connected = false;
        return DirectRouteConnectStatus::PayloadSendFailed;
    }
    if (route.route.postConnectPayload && route.route.postConnectPayloadLen > 0u) {
        const cxet::api::MarketDataConnectorPolicy marketPolicy = cxet::api::marketDataPolicyForConfig(*route.route.config);
        if (marketPolicy.readOneBeforePostConnectPayload) {
            thread_local MessageBuffer welcomeBuf;
            (void)route.client.runOne(welcomeBuf, 5000u);
        }
        if (!route.client.send(route.route.postConnectPayload, route.route.postConnectPayloadLen)) {
            route.client.disconnect();
            route.connected = false;
            return DirectRouteConnectStatus::PostConnectPayloadSendFailed;
        }
    }
    cxet::metrics::resetWsDataLatencyBook(route.dataLatencyBook);
    route.lastKeepaliveCheckTsc = cxet::probes::captureTsc();
    return DirectRouteConnectStatus::Ok;
}

cxet::api::market::PublicMarketDataStatus connectRecorderMarketDataRoute(
    cxet::api::market::PublicMarketDataRoute& route) noexcept {
    const DirectRouteConnectStatus status = connectMarketDataRouteNoTradingSync(route);
    switch (status) {
        case DirectRouteConnectStatus::Ok: return cxet::api::market::PublicMarketDataStatus::Ok;
        case DirectRouteConnectStatus::BadConfig: return cxet::api::market::PublicMarketDataStatus::BadConfig;
        case DirectRouteConnectStatus::WsConnectFailed:
        case DirectRouteConnectStatus::PayloadSendFailed:
        case DirectRouteConnectStatus::PostConnectPayloadSendFailed:
            return cxet::api::market::PublicMarketDataStatus::ConnectFailed;
    }
    return cxet::api::market::PublicMarketDataStatus::ConnectFailed;
}
void CaptureCoordinator::directBookTickerLoop_(CaptureConfig config) noexcept {
    Symbol symbol{};
    symbol.copyFrom(config.symbols.empty() ? "" : config.symbols.front().c_str());

    constexpr canon::FieldId kFields[] = {
        canon::kFieldIdBidPrice,
        canon::kFieldIdBidQty,
        canon::kFieldIdAskPrice,
        canon::kFieldIdAskQty,
        canon::kFieldIdTimestamp,
    };

    cxet::api::market::RedundantPublicMarketDataRoute redundantRoute{};
    cxet::api::market::RedundantPublicMarketDataRouteConfig routeConfig{};
    routeConfig.exchange = exchangeIdFromConfig(config.exchange);
    routeConfig.market = marketTypeFromConfig(config.market);
    routeConfig.symbol = symbol;
    routeConfig.apiSlot = 1u;
    routeConfig.stream = cxet::api::market::PublicMarketDataStream::BookTicker;
    routeConfig.wirePreference = wirePreferenceForConfig(config);
    routeConfig.requestedFields = Span<const canon::FieldId>(kFields, sizeof(kFields) / sizeof(kFields[0]));
    routeConfig.stopRequested = &bookTickerStop_;
    routeConfig.maxEvents = 0u;
    routeConfig.maxReconnectAttempts = 0u;
    routeConfig.pingIntervalMs = kRecorderQuietStreamKeepaliveMs;
    routeConfig.pollTimeoutMs = 1u;
    routeConfig.captureLatency = cxet::metrics::latencyEnabled();
    routeConfig.staleNs = kRecorderBookTickerStaleReconnectNs;
    routeConfig.reconnectRetryNs = static_cast<std::uint64_t>(kRecorderReconnectRetryMs) * 1000000ull;
    routeConfig.connectFn = connectRecorderMarketDataRoute;

    char routeError[256]{};
    if (!redundantRoute.prepare(routeConfig, routeError, sizeof(routeError))) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = routeError[0] != '\0' ? routeError : "bookticker direct redundant route prepare failed";
        bookTickerRunning_.store(false, std::memory_order_release);
        marketDataRunning_.store(false, std::memory_order_release);
        return;
    }

    auto connectDirectRoute = [&]() noexcept -> bool {
        while (!marketDataStop_.load(std::memory_order_acquire)
               && desiredBookTicker_.load(std::memory_order_acquire)
               && !bookTickerStop_.load(std::memory_order_acquire)) {
            if (redundantRoute.connectAll()) return true;
            const auto* route = redundantRoute.routeAt(redundantRoute.activeSlot());
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = std::string{"bookticker direct redundant route connect failed"}
                    + " host=" + (route && route->route.endpointHost ? route->route.endpointHost : "<null>")
                    + " path=" + std::string(route && route->route.connectPath ? route->route.connectPath : "<null>",
                                              route && route->route.connectPath ? route->route.connectPathLen : 6u)
                    + " payload=" + std::string(route && route->route.payload ? route->route.payload : "<null>",
                                                 route && route->route.payload ? std::min<std::size_t>(route->route.payloadLen, 160u) : 6u)
                    + " post_payload=" + std::string(route && route->route.postConnectPayload ? route->route.postConnectPayload : "<null>",
                                                      route && route->route.postConnectPayload ? std::min<std::size_t>(route->route.postConnectPayloadLen, 160u) : 6u)
                    + " ws_error=" + std::to_string(route ? route->client.lastConnectError() : 0);
            }
            if (!sleepCaptureStopAware(&bookTickerStop_, kRecorderReconnectRetryMs)) break;
        }
        return false;
    };

    if (!connectDirectRoute()) {
        if (bookTickerCount_.load(std::memory_order_relaxed) == 0u) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (lastError_.empty()) lastError_ = "bookticker direct route connect stopped before first row";
        }
        bookTickerRunning_.store(false, std::memory_order_release);
        marketDataRunning_.store(false, std::memory_order_release);
        return;
    }

    std::uint64_t skippedPolls = 0u;
    while (!marketDataStop_.load(std::memory_order_acquire)
           && desiredBookTicker_.load(std::memory_order_acquire)
           && !bookTickerStop_.load(std::memory_order_acquire)) {
        cxet::api::market::RedundantPublicMarketDataPollEvent events[1]{};
        const std::size_t eventCount = redundantRoute.pollOnce(events, 1u);
        if (eventCount != 0u && events[0].status == cxet::api::market::PublicMarketDataStatus::Parsed && events[0].snapshot) {
            cxet::composite::BookTickerRuntimeV1 bookTicker{};
            cxet::composite::StreamMeta meta{};
            if (!readBookTickerSnapshot(*events[0].snapshot, bookTicker, meta)) continue;
            bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
            const auto sequenceIds = nextEventSequenceIds(bookTickerCaptureSeq_, ingestSeq_);
            const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(bookTicker, meta);
            const auto row = makeBookTickerRow(capturedBookTicker, config.exchange, config.market, sequenceIds);
            local_exchange::globalLocalOrderEngine().onBookTicker(capturedBookTicker);
            auto aliases = config.bookTickerAliases;
            for (const auto* requiredAlias : {"bidQty", "askQty"}) {
                if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
            }
            const auto jsonLine = renderBookTickerJsonLine(row, aliases);
            local_exchange::globalLocalMarketDataBus().publish("bookticker", row.symbol, jsonLine);
            const auto liveStatus = liveStore_.appendBookTicker(row);
            const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendBookTickerLine(row, jsonLine) : liveStatus;
            if (!isOk(fileStatus)) {
                metrics::recordCaptureWriteError("bookticker");
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "bookticker: failed to write bookticker.jsonl";
                marketDataStop_.store(true, std::memory_order_release);
                break;
            }
            const std::uint64_t localNowNs = static_cast<std::uint64_t>(internal::nowNs());
            metrics::recordCaptureEvent("bookticker", capturedBookTicker.tsNs,
                                        static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                        localNowNs);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (events[0].promoted) {
                    lastError_ = "bookticker direct redundant route promoted standby";
                } else if (lastError_.rfind("bookticker direct ", 0u) == 0u) {
                    lastError_.clear();
                }
            }
            continue;
        }
        ++skippedPolls;
        const auto* route0 = redundantRoute.routeAt(0u);
        const auto* route1 = redundantRoute.routeAt(1u);
        const bool anyConnected = (route0 && route0->connected) || (route1 && route1->connected);
        if (!anyConnected) (void)sleepCaptureStopAware(&bookTickerStop_, 1u);
    }

    redundantRoute.closeAll();
    bookTickerRunning_.store(false, std::memory_order_release);
    marketDataRunning_.store(false, std::memory_order_release);
    if (bookTickerCount_.load(std::memory_order_relaxed) == 0u) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (lastError_.empty()) {
            const auto* snapshot = redundantRoute.activeSnapshot();
            lastError_ = "bookticker direct route ended without rows; skipped_polls=" + std::to_string(skippedPolls)
                + " frames=" + std::to_string(snapshot ? snapshot->frames : 0u)
                + " parsed=" + std::to_string(snapshot ? snapshot->parsedFrames : 0u)
                + " failures=" + std::to_string(snapshot ? snapshot->parseFailures : 0u)
                + " last_status=" + marketDataStatusName(snapshot ? snapshot->lastStatus : cxet::api::market::PublicMarketDataStatus::NoFrame);
        }
    }
}

void CaptureCoordinator::marketDataManagerLoop_(CaptureConfig config) noexcept {
    if (desiredBookTicker_.load(std::memory_order_acquire)
        && !desiredTrades_.load(std::memory_order_acquire)
        && !desiredOrderbook_.load(std::memory_order_acquire)) {
        directBookTickerLoop_(std::move(config));
        return;
    }

    struct RecorderRedundantChannel {
        cxet::api::market::PublicMarketDataStream stream{cxet::api::market::PublicMarketDataStream::BookTicker};
        canon::FieldId requestedFields[cxet::kMaxRequestedFields]{};
        std::size_t requestedFieldCount{0u};
        cxet::api::market::RedundantPublicMarketDataRoute route{};
    };

    constexpr std::size_t kRecorderRedundantChannelCap = 3u;
    auto channels = std::make_unique<RecorderRedundantChannel[]>(kRecorderRedundantChannelCap);
    std::size_t channelCount = 0u;
    std::uint8_t appliedMask = 0u;

    auto closeChannels = [&]() noexcept {
        for (std::size_t i = 0u; i < channelCount; ++i) channels[i].route.closeAll();
        channelCount = 0u;
    };

    auto rebuildDesired = [&]() -> bool {
        closeChannels();
        const bool wantTrades = desiredTrades_.load(std::memory_order_acquire);
        const bool wantBookTicker = desiredBookTicker_.load(std::memory_order_acquire);
        const bool wantOrderbook = desiredOrderbook_.load(std::memory_order_acquire);
        const std::string symbolText = config.symbols.empty() ? std::string{} : config.symbols.front();
        Symbol symbol{};
        symbol.copyFrom(symbolText.c_str());
        auto append = [&](cxet::api::market::PublicMarketDataStream stream,
                                const std::vector<std::string>& aliases,
                                cxet::UnifiedRequestBuilder builder) mutable -> bool {
            std::string fieldError;
            if (!internal::applyRequestedAliases(aliases, builder, fieldError)) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = fieldError;
                return false;
            }
            if (channelCount >= kRecorderRedundantChannelCap) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "too many recorder market-data channels";
                return false;
            }
            const auto fields = builder.requestedFields();
            if (fields.size() > cxet::kMaxRequestedFields) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "too many requested market-data fields";
                return false;
            }
            RecorderRedundantChannel& channel = channels[channelCount];
            channel.stream = stream;
            channel.requestedFieldCount = fields.size();
            for (std::size_t i = 0u; i < fields.size(); ++i) channel.requestedFields[i] = fields[i];

            cxet::api::market::RedundantPublicMarketDataRouteConfig routeConfig{};
            routeConfig.exchange = exchangeIdFromConfig(config.exchange);
            routeConfig.market = marketTypeFromConfig(config.market);
            routeConfig.symbol = symbol;
            routeConfig.apiSlot = 1u;
            routeConfig.stream = stream;
            routeConfig.captureLatency = cxet::metrics::latencyEnabled();
            routeConfig.wirePreference = wirePreferenceForConfig(config);
            routeConfig.maxReconnectAttempts = 0u;
            routeConfig.pingIntervalMs = stream == cxet::api::market::PublicMarketDataStream::Trades
                ? kRecorderTradesKeepaliveMs
                : kRecorderQuietStreamKeepaliveMs;
            routeConfig.pollTimeoutMs = 1u;
            routeConfig.requestedFields = Span<const canon::FieldId>(channel.requestedFields, channel.requestedFieldCount);
            routeConfig.stopRequested = &marketDataStop_;
            routeConfig.staleNs = kRecorderBookTickerStaleReconnectNs;
            routeConfig.reconnectRetryNs = static_cast<std::uint64_t>(kRecorderReconnectRetryMs) * 1000000ull;
            routeConfig.connectFn = connectRecorderMarketDataRoute;
            char errorBuf[256]{};
            if (!channel.route.prepare(routeConfig, errorBuf, sizeof(errorBuf))) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = errorBuf[0] != '\0' ? errorBuf : "recorder redundant market-data route prepare failed";
                return false;
            }
            if (!channel.route.connectAll()) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "recorder redundant market-data route connect pending";
            }
            ++channelCount;
            return true;
        };
        if (wantTrades && !append(cxet::api::market::PublicMarketDataStream::Trades,
                                  config.tradesAliases,
                                  internal::makeTradesBuilder(config))) {
            return false;
        }
        if (wantBookTicker) {
            auto aliases = config.bookTickerAliases;
            for (const auto* requiredAlias : {"bidQty", "askQty"}) {
                if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
            }
            if (!append(cxet::api::market::PublicMarketDataStream::BookTicker,
                        aliases,
                        internal::makeBookTickerBuilder(config))) {
                return false;
            }
        }
        if (wantOrderbook) {
            auto aliases = config.orderbookAliases;
            if (aliases.empty()) aliases = {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"};
            for (const auto* requiredAlias : {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"}) {
                if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
            }
            if (!append(cxet::api::market::PublicMarketDataStream::Orderbook,
                        aliases,
                        internal::makeOrderbookSubscribeBuilder(config))) {
                return false;
            }
        }
        return true;
    };

    auto desiredMask = [&]() noexcept -> std::uint8_t {
        std::uint8_t mask = 0u;
        if (desiredTrades_.load(std::memory_order_acquire)) mask |= 1u;
        if (desiredBookTicker_.load(std::memory_order_acquire)) mask |= 2u;
        if (desiredOrderbook_.load(std::memory_order_acquire)) mask |= 4u;
        return mask;
    };

    while (!marketDataStop_.load(std::memory_order_acquire)) {
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
        }

        std::size_t eventCount = 0u;
        for (std::size_t channelIndex = 0u; channelIndex < channelCount; ++channelIndex) {
            RecorderRedundantChannel& channel = channels[channelIndex];
            cxet::api::market::RedundantPublicMarketDataPollEvent events[1]{};
            const std::size_t polled = channel.route.pollOnce(events, 1u);
            if (polled == 0u || !events[0].snapshot) continue;
            ++eventCount;
            const auto* snapshot = events[0].snapshot;
            if (events[0].promoted) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "recorder redundant market-data route promoted standby";
            }
            const bool captureMetrics = cxet::metrics::shouldCaptureLatency();
            if (channel.stream == cxet::api::market::PublicMarketDataStream::Trades) {
                cxet::composite::TradeRuntimeV1 trade{};
                cxet::composite::StreamMeta meta{};
                if (!readTradeSnapshot(*snapshot, trade, meta)) continue;
                tradesCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(tradesCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedTrade = cxet_bridge::CxetCaptureBridge::captureTrade(trade, meta);
                const auto row = makeTradeRow(capturedTrade, config.exchange, config.market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onTrade(capturedTrade);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("trades", row.symbol, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = liveStore_.appendTrade(row);
                const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendTradeLine(row, jsonLine) : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("trades");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "trades: failed to write trades.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("trades", capturedTrade.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
            } else if (channel.stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
                cxet::composite::BookTickerRuntimeV1 bookTicker{};
                cxet::composite::StreamMeta meta{};
                if (!readBookTickerSnapshot(*snapshot, bookTicker, meta)) continue;
                bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(bookTickerCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(bookTicker, meta);
                const auto row = makeBookTickerRow(capturedBookTicker, config.exchange, config.market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onBookTicker(capturedBookTicker);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);
                auto aliases = config.bookTickerAliases;
                for (const auto* requiredAlias : {"bidQty", "askQty"}) {
                    if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
                }
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderBookTickerJsonLine(row, aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("bookticker", row.symbol, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = liveStore_.appendBookTicker(row);
                const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendBookTickerLine(row, jsonLine) : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("bookticker");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "bookticker: failed to write bookticker.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("bookticker", capturedBookTicker.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
            } else if (channel.stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                cxet::composite::OrderBookDeltaRuntimeV1 delta{};
                cxet::composite::StreamMeta meta{};
                if (!readOrderbookSnapshot(*snapshot, delta, meta)) continue;
                depthCount_.fetch_add(1, std::memory_order_acq_rel);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedDepth = cxet_bridge::CxetCaptureBridge::captureOrderBook(delta, meta);
                const auto row = makeDepthRow(capturedDepth);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                auto aliases = config.orderbookAliases;
                if (aliases.empty()) aliases = {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"};
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderDepthJsonLine(row, aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("orderbook.delta", std::string_view{meta.symbol.data}, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = liveStore_.appendDepth(row);
                const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendDepthLine(row, jsonLine) : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("depth");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "depth: failed to write depth.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("depth", capturedDepth.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
            }
        }
        if (eventCount == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    closeChannels();
    tradesRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    marketDataRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (marketDataThread_.joinable() && !marketDataRunning_.load(std::memory_order_acquire)) marketDataThread_.join();
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) liquidationsThread_.join();
}
Status CaptureCoordinator::writeSnapshotFile(const cxet::composite::OrderBookSnapshot& snapshot,
                                             std::uint64_t snapshotIndex,
                                             std::string_view snapshotKind,
                                             std::string_view source,
                                             bool trustedReplayAnchor) noexcept {
    std::string fileName;
    if (snapshotIndex == 0u) {
        fileName = std::string{channelFileName(ChannelKind::Snapshot)};
    } else {
        fileName = "snapshot_";
        if (snapshotIndex < 10u) {
            fileName += "00";
        } else if (snapshotIndex < 100u) {
            fileName += "0";
        }
        fileName += std::to_string(snapshotIndex);
        fileName += ".json";
    }
    (void)snapshotKind;
    (void)source;
    (void)trustedReplayAnchor;
    const auto capturedSnapshot = cxet_bridge::CxetCaptureBridge::captureOrderBook(snapshot);
    const auto document = makeSnapshotDocument(capturedSnapshot);
    std::string snapshotPayload = renderSnapshotJson(document);
    if (!snapshotPayload.empty() && snapshotPayload.back() == '\n') snapshotPayload.pop_back();
    local_exchange::globalLocalMarketDataBus().publish("orderbook.snapshot", std::string_view{snapshot.symbol.data}, snapshotPayload);
    if (!isOk(eventSink_.appendSnapshot(document, snapshotIndex))) return Status::IoError;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (std::find(manifest_.snapshotFiles.begin(), manifest_.snapshotFiles.end(), fileName) == manifest_.snapshotFiles.end()) {
            manifest_.snapshotFiles.push_back(fileName);
        }
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), fileName) == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(fileName);
        }
    }
    snapshotCount_.fetch_add(1, std::memory_order_acq_rel);
    return Status::Ok;
}

}  // namespace hftrec::capture
