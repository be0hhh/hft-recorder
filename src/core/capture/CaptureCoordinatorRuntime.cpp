#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "api/run/RunByConfig.hpp"
#include "api/run/RunByConfigFetch.hpp"
#include "api/dispatch/BuildFromConfig.hpp"
#include "api/connector/ExchangeProductConnector.hpp"
#include "api/orderbook/OrderBookSnapshotFetch.hpp"
#include "api/route/RoutePlan.hpp"
#include "api/market/MarketDataReactor.hpp"
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
#include "primitives/composite/OrderBookSnapshot.hpp"

namespace hftrec::capture {

namespace {

constexpr unsigned kRecorderTradesKeepaliveMs = 0u;
constexpr unsigned kRecorderQuietStreamKeepaliveMs = 5000u;
constexpr unsigned kRecorderReconnectRetryMs = 100u;

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

bool containsLevel(const std::vector<replay::PricePair>& levels, const replay::PricePair& needle) noexcept {
    return std::find_if(levels.begin(), levels.end(), [&](const replay::PricePair& level) noexcept {
        return level.priceE8 == needle.priceE8 && level.side == needle.side;
    }) != levels.end();
}

void normalizeFixedDepthSnapshotDelta(replay::DepthRow& row,
                                      std::vector<replay::PricePair>& previousLevels) {
    std::vector<replay::PricePair> currentLevels;
    currentLevels.reserve(row.levels.size());
    for (const auto& level : row.levels) {
        if (level.qtyE8 > 0) currentLevels.push_back(level);
    }
    for (const auto& previous : previousLevels) {
        if (!containsLevel(currentLevels, previous)) {
            row.levels.push_back(replay::PricePair{previous.priceE8, 0, previous.side});
        }
    }
    previousLevels = std::move(currentLevels);
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
constexpr std::int64_t kRecorderRestDecimalScale = 100000000LL;

bool decimalTokenToScaled(std::string_view token, std::int64_t& out) noexcept {
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t' || token.front() == '\r' || token.front() == '\n')) token.remove_prefix(1u);
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r' || token.back() == '\n')) token.remove_suffix(1u);
    if (token.size() >= 2u && token.front() == '"' && token.back() == '"') {
        token.remove_prefix(1u);
        token.remove_suffix(1u);
    }
    if (token.empty()) return false;
    bool negative = false;
    if (token.front() == '-') {
        negative = true;
        token.remove_prefix(1u);
    }
    std::int64_t whole = 0;
    std::int64_t frac = 0;
    std::int64_t fracScale = kRecorderRestDecimalScale;
    bool sawDigit = false;
    bool afterDot = false;
    for (char ch : token) {
        if (ch == '.') {
            if (afterDot) return false;
            afterDot = true;
            continue;
        }
        if (ch < '0' || ch > '9') return false;
        sawDigit = true;
        const int digit = ch - '0';
        if (!afterDot) {
            whole = whole * 10 + digit;
        } else if (fracScale > 1) {
            fracScale /= 10;
            frac += static_cast<std::int64_t>(digit) * fracScale;
        }
    }
    if (!sawDigit) return false;
    out = whole * kRecorderRestDecimalScale + frac;
    if (negative) out = -out;
    return true;
}

void skipJsonSpace(std::string_view text, std::size_t& pos) noexcept {
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        ++pos;
    }
}

bool readJsonScalarToken(std::string_view text, std::size_t& pos, std::string_view& token) noexcept {
    skipJsonSpace(text, pos);
    if (pos >= text.size()) return false;
    const std::size_t start = pos;
    if (text[pos] == '"') {
        ++pos;
        while (pos < text.size() && text[pos] != '"') ++pos;
        if (pos >= text.size()) return false;
        ++pos;
        token = text.substr(start, pos - start);
        return true;
    }
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == ',' || ch == ']' || ch == '}' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') break;
        ++pos;
    }
    token = text.substr(start, pos - start);
    return !token.empty();
}

bool parseJsonUintFieldMs(std::string_view text, std::string_view key, std::uint64_t& out) noexcept {
    const std::size_t keyPos = text.find(key);
    if (keyPos == std::string_view::npos) return false;
    std::size_t pos = text.find(':', keyPos + key.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    std::string_view token;
    if (!readJsonScalarToken(text, pos, token)) return false;
    while (!token.empty() && token.front() == '"') token.remove_prefix(1u);
    while (!token.empty() && token.back() == '"') token.remove_suffix(1u);
    std::uint64_t value = 0u;
    bool sawDigit = false;
    for (char ch : token) {
        if (ch < '0' || ch > '9') break;
        sawDigit = true;
        value = value * 10u + static_cast<std::uint64_t>(ch - '0');
    }
    if (!sawDigit) return false;
    out = value;
    return true;
}

bool parseRestLevels(std::string_view text,
                     const char* key,
                     Side side,
                     cxet::composite::OrderBookSnapshot& out) noexcept {
    const std::string_view keyText{key};
    std::size_t pos = text.find(keyText);
    if (pos == std::string_view::npos) return false;
    pos = text.find('[', pos + keyText.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < text.size()) {
        skipJsonSpace(text, pos);
        if (pos >= text.size() || text[pos] == ']') return true;
        if (text[pos] == ',') {
            ++pos;
            continue;
        }
        if (text[pos] != '[') {
            ++pos;
            continue;
        }
        ++pos;
        std::string_view priceToken;
        std::string_view qtyToken;
        if (!readJsonScalarToken(text, pos, priceToken)) return false;
        skipJsonSpace(text, pos);
        if (pos < text.size() && text[pos] == ',') ++pos;
        if (!readJsonScalarToken(text, pos, qtyToken)) return false;
        std::int64_t priceRaw = 0;
        std::int64_t qtyRaw = 0;
        if (decimalTokenToScaled(priceToken, priceRaw) && decimalTokenToScaled(qtyToken, qtyRaw)) {
            Price price{};
            price.raw = priceRaw;
            Amount qty{};
            qty.raw = qtyRaw;
            if (!cxet::composite::appendOrderBookLevel(out, price, qty, side)) return true;
        }
        while (pos < text.size() && text[pos] != ']') ++pos;
        if (pos < text.size()) ++pos;
    }
    return true;
}

bool parseRestOrderbookJson(std::string_view text,
                            cxet::composite::OrderBookSnapshot& snapshot) noexcept {
    cxet::composite::resetOrderBookSnapshotHeader(snapshot);
    std::uint64_t tsMs = 0u;
    if (parseJsonUintFieldMs(text, "\"ts\"", tsMs)
        || parseJsonUintFieldMs(text, "\"requestTime\"", tsMs)
        || parseJsonUintFieldMs(text, "\"E\"", tsMs)
        || parseJsonUintFieldMs(text, "\"T\"", tsMs)
        || parseJsonUintFieldMs(text, "\"timestamp\"", tsMs)) {
        snapshot.ts.raw = static_cast<std::int64_t>(tsMs * 1000000ULL);
    } else {
        snapshot.ts.raw = internal::nowNs();
    }
    if (!parseRestLevels(text, "\"bids\"", Side::Buy(), snapshot)) return false;
    if (!parseRestLevels(text, "\"asks\"", Side::Sell(), snapshot)) return false;
    if (snapshot.levelCount.raw != 0u) return true;
    if (!parseRestLevels(text, "\"b\"", Side::Buy(), snapshot)) return false;
    if (!parseRestLevels(text, "\"a\"", Side::Sell(), snapshot)) return false;
    return snapshot.levelCount.raw != 0u;
}


bool shouldFetchInitialOrderbookSnapshot(const CaptureConfig& config) noexcept {
    return !textEqualsAscii(config.exchange, "bitget");
}

bool fetchInitialOrderbookSnapshot(const CaptureConfig& config,
                                   cxet::composite::OrderBookSnapshot& snapshot) noexcept {
    Symbol symbol{};
    symbol.copyFrom(config.symbols.empty() ? "" : config.symbols.front().c_str());

    cxet::api::orderbook::OrderBookSnapshotRequest request{};
    request.exchange = exchangeIdFromConfig(config.exchange);
    request.market = marketTypeFromConfig(config.market);
    request.symbol = symbol;

    cxet::UnifiedRequestBuilder builder = cxet::api::orderbook::makeOrderBookSnapshotBuilder(request);
    const auto* routeConfig = cxet::api::findObjectConfig(static_cast<std::uint8_t>(builder.operation()),
                                                          builder.objectRaw(),
                                                          builder.exchange().raw,
                                                          builder.market().raw,
                                                          builder.typeRaw(),
                                                          builder.subtypeRaw());
    if (!routeConfig || !routeConfig->parseOrderBookFn) return false;

    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    if (!cxet::api::buildFromConfig(builder, *routeConfig, requestBuf)) return false;
    if (!cxet::api::detail::fetchRestByConfig(*routeConfig, requestBuf, recvBuf)) return false;
    cxet::composite::resetOrderBookSnapshotHeader(snapshot);
    const bool parsed = routeConfig->parseOrderBookFn(recvBuf,
                                                      builder,
                                                      request.exchange,
                                                      &snapshot,
                                                      builder.requestedFields());
    if (parsed && snapshot.levelCount.raw != 0u) return true;
    return parseRestOrderbookJson(std::string_view{recvBuf.data(), recvBuf.size()}, snapshot);
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

struct RecorderMarketDataChannel {
    cxet::api::market::PublicMarketDataStream stream{cxet::api::market::PublicMarketDataStream::BookTicker};
    canon::FieldId requestedFields[cxet::kMaxRequestedFields]{};
    std::size_t requestedFieldCount{0u};
    cxet::UnifiedRequestBuilder builder{};
    MessageBuffer payloadBuf{};
    MessageBuffer workBuf{};
    MessageBuffer recvBuf{};
    cxet::api::market::PublicMarketDataSnapshot snapshot{};
    cxet::api::market::PublicMarketDataRoute route{};
};

bool prepareRecorderMarketDataChannel(RecorderMarketDataChannel& channel,
                                      cxet::api::market::PublicMarketDataStream stream,
                                      cxet::UnifiedRequestBuilder builder,
                                      cxet::api::market::PublicMarketDataWirePreference wirePreference,
                                      Span<const canon::FieldId> requestedFields,
                                      std::atomic<bool>* stopRequested,
                                      unsigned pingIntervalMs,
                                      bool captureLatency) noexcept {
    if (requestedFields.size() > cxet::kMaxRequestedFields) return false;
    channel.stream = stream;
    channel.builder = builder;
    channel.requestedFieldCount = requestedFields.size();
    for (std::size_t i = 0u; i < requestedFields.size(); ++i) channel.requestedFields[i] = requestedFields[i];

    cxet::api::market::PublicMarketDataRouteConfig routeConfig{};
    routeConfig.builder = &channel.builder;
    routeConfig.payloadBuf = &channel.payloadBuf;
    routeConfig.combinedPayloadBuf = &channel.workBuf;
    routeConfig.recvBuf = &channel.recvBuf;
    routeConfig.snapshot = &channel.snapshot;
    routeConfig.stream = stream;
    routeConfig.wirePreference = wirePreference;
    routeConfig.requestedFields = Span<const canon::FieldId>(channel.requestedFields, channel.requestedFieldCount);
    routeConfig.stopRequested = stopRequested;
    routeConfig.maxEvents = 0u;
    routeConfig.maxReconnectAttempts = 0u;
    routeConfig.pingIntervalMs = pingIntervalMs;
    routeConfig.pollTimeoutMs = 1u;
    routeConfig.captureLatency = captureLatency;
    return cxet::api::market::preparePublicMarketDataRoute(channel.route, routeConfig);
}
void CaptureCoordinator::directBookTickerLoop_(CaptureConfig config) noexcept {
    constexpr canon::FieldId kFields[] = {
        canon::kFieldIdBidPrice,
        canon::kFieldIdBidQty,
        canon::kFieldIdAskPrice,
        canon::kFieldIdAskQty,
        canon::kFieldIdTimestamp,
    };

    auto builder = internal::makeBookTickerBuilder(config);
    std::string fieldError;
    if (!internal::applyRequestedAliases(config.bookTickerAliases, builder, fieldError)) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = fieldError;
        bookTickerRunning_.store(false, std::memory_order_release);
        marketDataRunning_.store(false, std::memory_order_release);
        return;
    }

    RecorderMarketDataChannel channel{};
    if (!prepareRecorderMarketDataChannel(channel,
                                          cxet::api::market::PublicMarketDataStream::BookTicker,
                                          builder,
                                          wirePreferenceForConfig(config),
                                          Span<const canon::FieldId>(kFields, sizeof(kFields) / sizeof(kFields[0])),
                                          &bookTickerStop_,
                                          kRecorderQuietStreamKeepaliveMs,
                                          cxet::metrics::latencyEnabled())) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = "bookticker direct route prepare failed";
        bookTickerRunning_.store(false, std::memory_order_release);
        marketDataRunning_.store(false, std::memory_order_release);
        return;
    }

    auto connectDirectRoute = [&]() noexcept -> bool {
        while (!marketDataStop_.load(std::memory_order_acquire)
               && desiredBookTicker_.load(std::memory_order_acquire)
               && !bookTickerStop_.load(std::memory_order_acquire)) {
            if (connectRecorderMarketDataRoute(channel.route) == cxet::api::market::PublicMarketDataStatus::Ok) return true;
            const auto* route = &channel.route;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = std::string{"bookticker direct route connect failed"}
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
        const auto status = cxet::api::market::pollPublicMarketDataRouteOnce(channel.route);
        if (status == cxet::api::market::PublicMarketDataStatus::Parsed) {
            cxet::composite::BookTickerRuntimeV1 bookTicker{};
            cxet::composite::StreamMeta meta{};
            if (!readBookTickerSnapshot(channel.snapshot, bookTicker, meta)) continue;
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
                if (lastError_.rfind("bookticker direct ", 0u) == 0u) {
                    lastError_.clear();
                }
            }
            continue;
        }
        ++skippedPolls;
        if (status == cxet::api::market::PublicMarketDataStatus::Disconnected
            || status == cxet::api::market::PublicMarketDataStatus::ConnectFailed) {
            cxet::api::market::closePublicMarketDataRoute(channel.route);
            (void)connectDirectRoute();
        } else if (!channel.route.connected) {
            (void)sleepCaptureStopAware(&bookTickerStop_, 1u);
        }
    }

    cxet::api::market::closePublicMarketDataRoute(channel.route);
    bookTickerRunning_.store(false, std::memory_order_release);
    marketDataRunning_.store(false, std::memory_order_release);
    if (bookTickerCount_.load(std::memory_order_relaxed) == 0u) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (lastError_.empty()) {
            const auto* snapshot = &channel.snapshot;
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

    constexpr std::size_t kRecorderRedundantChannelCap = 3u;
    auto channels = std::make_unique<RecorderMarketDataChannel[]>(kRecorderRedundantChannelCap);
    std::size_t channelCount = 0u;
    std::uint8_t appliedMask = 0u;
    bool initialOrderbookSeedAttempted = depthCount_.load(std::memory_order_acquire) != 0u;
    std::vector<replay::PricePair> bitgetPreviousOrderbookLevels{};

    auto closeChannels = [&]() noexcept {
        for (std::size_t i = 0u; i < channelCount; ++i) {
            cxet::api::market::closePublicMarketDataRoute(channels[i].route);
        }
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
            RecorderMarketDataChannel& channel = channels[channelCount];
            const unsigned pingIntervalMs = stream == cxet::api::market::PublicMarketDataStream::Trades
                ? kRecorderTradesKeepaliveMs
                : kRecorderQuietStreamKeepaliveMs;
            if (!prepareRecorderMarketDataChannel(channel,
                                                  stream,
                                                  builder,
                                                  wirePreferenceForConfig(config),
                                                  fields,
                                                  &marketDataStop_,
                                                  pingIntervalMs,
                                                  cxet::metrics::latencyEnabled())) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "recorder market-data route prepare failed";
                return false;
            }
            if (connectRecorderMarketDataRoute(channel.route) != cxet::api::market::PublicMarketDataStatus::Ok) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "recorder market-data route connect pending";
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
            if (!initialOrderbookSeedAttempted) {
                initialOrderbookSeedAttempted = true;
                cxet::composite::OrderBookSnapshot initialSnapshot{};
                if (!shouldFetchInitialOrderbookSnapshot(config)) {
                    // Bitget has no CXET REST orderbook route here; use WS orderbook only.
                } else if (!fetchInitialOrderbookSnapshot(config, initialSnapshot)) {
                    metrics::recordSnapshotFetchFailure("snapshot");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "orderbook: initial REST snapshot fetch failed; continuing with WS depth";
                } else {
                    const auto capturedSnapshot = cxet_bridge::CxetCaptureBridge::captureOrderBook(initialSnapshot);
                    const auto row = makeDepthRow(capturedSnapshot);
                    auto aliases = config.orderbookAliases;
                    if (aliases.empty()) aliases = {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"};
                    const auto jsonLine = renderDepthJsonLine(row, aliases);
                    const auto liveStatus = liveStore_.appendDepth(row);
                    const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendDepthLine(row, jsonLine) : liveStatus;
                    if (!isOk(fileStatus)) {
                        metrics::recordCaptureWriteError("depth");
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        lastError_ = "orderbook: failed to write initial REST snapshot into depth.jsonl";
                        return false;
                    }
                    depthCount_.fetch_add(1, std::memory_order_acq_rel);
                    metrics::recordCaptureEvent("depth", capturedSnapshot.tsNs,
                                                static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                                static_cast<std::uint64_t>(internal::nowNs()));
                }
            }
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
            RecorderMarketDataChannel& channel = channels[channelIndex];
            const auto status = cxet::api::market::pollPublicMarketDataRouteOnce(channel.route);
            if (status == cxet::api::market::PublicMarketDataStatus::Disconnected
                || status == cxet::api::market::PublicMarketDataStatus::ConnectFailed) {
                cxet::api::market::closePublicMarketDataRoute(channel.route);
                (void)connectRecorderMarketDataRoute(channel.route);
            }
            if (status != cxet::api::market::PublicMarketDataStatus::Parsed) continue;
            ++eventCount;
            const auto* snapshot = &channel.snapshot;
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
                auto row = makeDepthRow(capturedDepth);
                if (textEqualsAscii(config.exchange, "bitget")) {
                    normalizeFixedDepthSnapshotDelta(row, bitgetPreviousOrderbookLevels);
                }
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
    local_exchange::globalLocalMarketDataBus().publish("orderbook.snapshot", std::string_view{}, snapshotPayload);
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
