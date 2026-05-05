#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>


#include "api/run/RunByConfig.hpp"
#include "api/market/MarketDataReactor.hpp"
#include "api/market/PublicMarketDataSubscriptionManager.hpp"
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
constexpr unsigned kRecorderQuietStreamKeepaliveMs = 30000u;

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

void CaptureCoordinator::marketDataManagerLoop_(CaptureConfig config) noexcept {
    cxet::api::market::PublicMarketDataSubscriptionManager manager{};
    std::uint8_t appliedMask = 0u;

    auto rebuildDesired = [&](std::vector<cxet::api::market::PublicMarketDataDesiredChannel>& desired) -> bool {
        desired.clear();
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
            cxet::api::market::PublicMarketDataDesiredChannel channel{};
            channel.exchange = exchangeIdFromConfig(config.exchange);
            channel.market = marketTypeFromConfig(config.market);
            channel.symbol = symbol;
            channel.stream = stream;
            channel.apiSlot = 1u;
            channel.captureLatency = cxet::metrics::latencyEnabled();
            channel.wirePreference = wirePreferenceForConfig(config);
            channel.maxReconnectAttempts = 10u;
            channel.pingIntervalMs = stream == cxet::api::market::PublicMarketDataStream::Trades
                ? kRecorderTradesKeepaliveMs
                : kRecorderQuietStreamKeepaliveMs;
            channel.pollTimeoutMs = 1u;
            const auto fields = builder.requestedFields();
            if (fields.size() > cxet::api::market::kMaxManagedMarketDataRequestedFields) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "too many requested market-data fields";
                return false;
            }
            channel.requestedFieldCount = fields.size();
            for (std::size_t i = 0u; i < fields.size(); ++i) channel.requestedFields[i] = fields[i];
            desired.push_back(channel);
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
            std::vector<cxet::api::market::PublicMarketDataDesiredChannel> desired;
            if (!rebuildDesired(desired)) break;
            char errorBuf[256]{};
            cxet::api::market::PublicMarketDataApplyResult result{};
            const bool applied = manager.applyDesired(
                Span<const cxet::api::market::PublicMarketDataDesiredChannel>(desired.data(), desired.size()),
                &result,
                errorBuf,
                sizeof(errorBuf));
            if (!applied) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = errorBuf[0] != '\0' ? errorBuf : "market-data manager apply failed";
                break;
            }
            appliedMask = mask;
        }

        cxet::api::market::PublicMarketDataPollEvent events[64]{};
        const std::size_t eventCount = manager.pollOnce(events, sizeof(events) / sizeof(events[0]));
        const std::size_t visibleEvents = eventCount < (sizeof(events) / sizeof(events[0]))
            ? eventCount
            : (sizeof(events) / sizeof(events[0]));
        for (std::size_t e = 0u; e < visibleEvents; ++e) {
            const auto* managed = manager.channelAt(events[e].channelIndex);
            const auto* snapshot = manager.routeSnapshotBySlot(events[e].routeSlot);
            if (!managed || !snapshot) continue;
            const bool captureMetrics = cxet::metrics::shouldCaptureLatency();
            if (managed->stream == cxet::api::market::PublicMarketDataStream::Trades) {
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
            } else if (managed->stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
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
            } else if (managed->stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
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

    manager.closeAll();
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
