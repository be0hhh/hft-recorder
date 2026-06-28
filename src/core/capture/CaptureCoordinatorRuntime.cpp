#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "canon/MarketMapping.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalMarketDataBus.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/metrics/Metrics.hpp"
#include "hft_trader/runtime/config/RuntimeConfig.hpp"
#include "hft_trader/runtime/history/candles/CandleHistoryLoader.hpp"
#include "hft_trader/runtime/history/orderbook/OrderBookSnapshotLoader.hpp"
#include "hft_trader/runtime/history/trades/TradeHistoryLoader.hpp"
#include "hft_trader/runtime/market/MarketDataRuntime.hpp"
#include "primitives/composite/OrderBookTapeRuntimeV1.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/TieredCandleHistory.hpp"
#include "primitives/composite/StreamMeta.hpp"

#include "metrics/MetricsControl.hpp"
#include "metrics/Probes.hpp"
#include "probes/TimeDelta.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"

namespace hftrec::capture {

namespace {

constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;
constexpr std::int64_t kMarketDataLifecyclePollIntervalNs = 250'000'000LL;
constexpr std::int64_t kTradesHistoryWarmupMaxSec = 86400;
constexpr std::size_t kTradesHistoryWarmupTargetRows = 0u;
constexpr std::uint32_t kTradesHistoryWarmupPageLimit = 1000u;
constexpr std::uint32_t kDetailedCandlesDefaultLimit = 5000u;
constexpr std::uint32_t kDetailedCandlesMaxLimit = 1'000'000u;

void copySymbolFromText(Symbol& out, std::string_view text) noexcept {
    char buffer[rawdata::SymbolMaxBytes]{};
    const std::size_t copyLen = std::min(text.size(), sizeof(buffer) - 1u);
    for (std::size_t i = 0u; i < copyLen; ++i) buffer[i] = text[i];
    out.copyFrom(buffer);
}

std::int64_t candleTierFromTimeframe(std::string_view timeframe) noexcept {
    if (timeframe == "1m") return 1;
    if (timeframe == "10m" || timeframe == "15m") return 2;
    if (timeframe == "1d") return 3;
    return 0;
}

std::string sanitizedTimeframeSuffix(std::string_view timeframe) {
    std::string out;
    out.reserve(timeframe.size());
    for (char c : timeframe) {
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z')) {
            out.push_back(c);
        }
    }
    return out.empty() ? std::string{"unknown"} : out;
}

std::string detailedCandlesRelativePath(std::string_view timeframe, bool detailed) {
    const auto suffix = sanitizedTimeframeSuffix(timeframe);
    return std::string{"jsonl/"} + (detailed ? "candles2_" : "candles_") + suffix + ".jsonl";
}

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

replay::LiquidationRow makeLiquidationRow(const cxet_bridge::CapturedLiquidationRow& liquidation,
                                          std::string_view exchange,
                                          std::string_view market,
                                          const EventSequenceIds& sequenceIds) noexcept {
    replay::LiquidationRow row{};
    row.symbol = liquidation.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(liquidation.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = liquidation.priceE8;
    row.qtyE8 = liquidation.qtyE8;
    row.avgPriceE8 = liquidation.avgPriceE8;
    row.filledQtyE8 = liquidation.filledQtyE8;
    row.side = liquidation.side;
    row.sideBuy = liquidation.sideBuy ? 1u : 0u;
    row.orderType = liquidation.orderType;
    row.timeInForce = liquidation.timeInForce;
    row.status = liquidation.status;
    row.sourceMode = liquidation.sourceMode;
    return row;
}

replay::TradeRow makeHistoricalTradeRow(const cxet::composite::TradePublic& trade,
                                        std::string_view exchange,
                                        std::string_view market,
                                        std::string_view identitySymbol,
                                        const EventSequenceIds& sequenceIds) {
    replay::TradeRow row{};
    row.tradeId = static_cast<std::uint64_t>(trade.id.raw);
    row.firstTradeId = static_cast<std::uint64_t>(trade.firstTradeId.raw);
    row.lastTradeId = static_cast<std::uint64_t>(trade.lastTradeId.raw);
    row.symbol = identitySymbol.empty() ? std::string{trade.symbol.data} : std::string{identitySymbol};
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(trade.ts.raw);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = static_cast<std::int64_t>(trade.price.raw);
    row.qtyE8 = static_cast<std::int64_t>(trade.amount.raw);
    row.quoteQtyE8 = static_cast<std::int64_t>(trade.quoteAmount.raw);
    row.side = static_cast<std::int64_t>(trade.side.raw);
    row.isBuyerMaker = trade.isBuyerMaker == canon::TriState::True ? 1u : 0u;
    row.sideBuy = static_cast<std::uint8_t>(trade.side.raw) == 1u ? 1u : 0u;
    return row;
}

bool tradeLessByEventTime(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if (lhs.tradeId != rhs.tradeId) return lhs.tradeId < rhs.tradeId;
    if (lhs.priceE8 != rhs.priceE8) return lhs.priceE8 < rhs.priceE8;
    if (lhs.qtyE8 != rhs.qtyE8) return lhs.qtyE8 < rhs.qtyE8;
    return lhs.captureSeq < rhs.captureSeq;
}

bool sameTradeEvent(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept {
    if (lhs.tradeId != 0u && rhs.tradeId != 0u) {
        return lhs.tradeId == rhs.tradeId && lhs.symbol == rhs.symbol;
    }
    return lhs.tsNs == rhs.tsNs
        && lhs.priceE8 == rhs.priceE8
        && lhs.qtyE8 == rhs.qtyE8
        && lhs.side == rhs.side
        && lhs.symbol == rhs.symbol;
}

const char* historicalTradeFeedKindName(cxet::api::trades::HistoricalTradeFeedKind kind) noexcept {
    switch (kind) {
        case cxet::api::trades::HistoricalTradeFeedKind::RawTrades: return "raw_trades";
        case cxet::api::trades::HistoricalTradeFeedKind::AggTrades: return "agg_trades";
        case cxet::api::trades::HistoricalTradeFeedKind::RecentTrades: return "recent_trades";
    }
    return "unknown";
}

const char* historicalTradesStatusName(cxet::api::trades::HistoricalTradesStatus status) noexcept {
    switch (status) {
        case cxet::api::trades::HistoricalTradesStatus::Ok: return "ok";
        case cxet::api::trades::HistoricalTradesStatus::Empty: return "empty";
        case cxet::api::trades::HistoricalTradesStatus::BadConfig: return "bad_config";
        case cxet::api::trades::HistoricalTradesStatus::UnsupportedRange: return "unsupported_range";
        case cxet::api::trades::HistoricalTradesStatus::RecentOnly: return "recent_only";
        case cxet::api::trades::HistoricalTradesStatus::FetchFailed: return "fetch_failed";
        case cxet::api::trades::HistoricalTradesStatus::ParseFailed: return "parse_failed";
        case cxet::api::trades::HistoricalTradesStatus::StoppedBySink: return "stopped_by_sink";
    }
    return "unknown";
}

struct TradesHistoryWarmupState {
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::mutex mutex{};
    std::vector<replay::TradeRow> historyRows{};
    cxet::api::trades::HistoricalTradesResult result{};
    std::string error{};
    std::int64_t requestedStartNs{0};
    std::int64_t requestedEndNs{0};
};

struct TradesHistorySinkContext {
    TradesHistoryWarmupState* state{nullptr};
    std::atomic<std::uint64_t>* tradesCaptureSeq{nullptr};
    std::atomic<std::uint64_t>* ingestSeq{nullptr};
    std::string exchange{};
    std::string market{};
    std::string identitySymbol{};
    std::size_t maxRows{0u};
    bool hitRowLimit{false};
};

bool appendHistoricalTradesToWarmup(void* userData,
                                    const cxet::composite::TradePublic* rows,
                                    std::size_t rowCount) noexcept {
    auto* context = static_cast<TradesHistorySinkContext*>(userData);
    if (context == nullptr || context->state == nullptr || context->tradesCaptureSeq == nullptr || context->ingestSeq == nullptr) return false;
    std::lock_guard<std::mutex> lock(context->state->mutex);
    context->state->historyRows.reserve(context->state->historyRows.size() + rowCount);
    for (std::size_t i = 0u; i < rowCount; ++i) {
        if (rows[i].ts.raw == 0u || rows[i].price.raw == 0u || rows[i].amount.raw == 0u) continue;
        if (context->maxRows != 0u && context->state->historyRows.size() >= context->maxRows) {
            context->hitRowLimit = true;
            return false;
        }
        const auto ids = nextEventSequenceIds(*context->tradesCaptureSeq, *context->ingestSeq);
        context->state->historyRows.push_back(
            makeHistoricalTradeRow(rows[i], context->exchange, context->market, context->identitySymbol, ids));
    }
    return true;
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

replay::MarkPriceRow makeMarkPriceRow(const cxet::composite::MarkPriceRuntimeV1& markPrice) noexcept {
    replay::MarkPriceRow row{};
    row.tsNs = static_cast<std::int64_t>(markPrice.ts.raw);
    row.markPriceE8 = static_cast<std::int64_t>(markPrice.markPrice.raw);
    return row;
}

replay::IndexPriceRow makeIndexPriceRow(const cxet::composite::IndexPriceRuntimeV1& indexPrice) noexcept {
    replay::IndexPriceRow row{};
    row.tsNs = static_cast<std::int64_t>(indexPrice.ts.raw);
    row.indexPriceE8 = static_cast<std::int64_t>(indexPrice.indexPrice.raw);
    return row;
}

replay::FundingRow makeFundingRow(const cxet::composite::FundingRuntimeV1& funding) noexcept {
    replay::FundingRow row{};
    row.tsNs = static_cast<std::int64_t>(funding.ts.raw);
    row.fundingRateE8 = static_cast<std::int64_t>(funding.fundingRate.raw);
    row.fundingTsNs = static_cast<std::int64_t>(funding.fundingTs.raw);
    row.nextFundingTsNs = static_cast<std::int64_t>(funding.nextFundingTs.raw);
    return row;
}

replay::PriceLimitRow makePriceLimitRow(const cxet::composite::PriceLimitRuntimeV1& priceLimit) noexcept {
    replay::PriceLimitRow row{};
    row.tsNs = static_cast<std::int64_t>(priceLimit.ts.raw);
    row.buyLimitE8 = static_cast<std::int64_t>(priceLimit.buyLimit.raw);
    row.sellLimitE8 = static_cast<std::int64_t>(priceLimit.sellLimit.raw);
    row.enabled = priceLimit.enabled != 0u ? 1u : 0u;
    return row;
}

bool sameFundingTuple(const replay::FundingRow& lhs, const replay::FundingRow& rhs) noexcept {
    return lhs.fundingRateE8 == rhs.fundingRateE8
        && lhs.fundingTsNs == rhs.fundingTsNs
        && lhs.nextFundingTsNs == rhs.nextFundingTsNs;
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

const char* marketStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept;

const char* publicMarketDataStatusName(cxet::api::market::PublicMarketDataStatus status) noexcept {
    switch (status) {
        case cxet::api::market::PublicMarketDataStatus::Ok: return "ok";
        case cxet::api::market::PublicMarketDataStatus::NoFrame: return "no_frame";
        case cxet::api::market::PublicMarketDataStatus::Parsed: return "parsed";
        case cxet::api::market::PublicMarketDataStatus::ParseSkipped: return "parse_skipped";
        case cxet::api::market::PublicMarketDataStatus::ParseFailed: return "parse_failed";
        case cxet::api::market::PublicMarketDataStatus::ConnectFailed: return "connect_failed";
        case cxet::api::market::PublicMarketDataStatus::Disconnected: return "disconnected";
        case cxet::api::market::PublicMarketDataStatus::Reconnected: return "reconnected";
        case cxet::api::market::PublicMarketDataStatus::BadConfig: return "bad_config";
        case cxet::api::market::PublicMarketDataStatus::UnsupportedRoute: return "unsupported_route";
        case cxet::api::market::PublicMarketDataStatus::Stopped: return "stopped";
        case cxet::api::market::PublicMarketDataStatus::SubscribeFailed: return "subscribe_failed";
    }
    return "unknown";
}

bool marketDataStatusNeedsOperatorDetail(cxet::api::market::PublicMarketDataStatus status) noexcept {
    return status == cxet::api::market::PublicMarketDataStatus::ParseFailed ||
           status == cxet::api::market::PublicMarketDataStatus::ConnectFailed ||
           status == cxet::api::market::PublicMarketDataStatus::Disconnected ||
           status == cxet::api::market::PublicMarketDataStatus::BadConfig ||
           status == cxet::api::market::PublicMarketDataStatus::UnsupportedRoute ||
           status == cxet::api::market::PublicMarketDataStatus::SubscribeFailed;
}

std::string marketDataRuntimeDiagnosticText(const hft_trader::runtime::MarketDataRuntime& runtime,
                                            std::string_view scope) {
    std::array<cxet::api::market::PublicMarketDataRouteDiagnostic, 8> diagnostics{};
    const std::size_t routeCount = runtime.manager().routeDiagnostics(diagnostics.data(), diagnostics.size());
    std::string out;
    const std::size_t count = std::min(routeCount, diagnostics.size());
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& diagnostic = diagnostics[i];
        if (!marketDataStatusNeedsOperatorDetail(diagnostic.lastStatus)) continue;
        if (!out.empty()) out += " | ";
        out += scope;
        out += ": route status=";
        out += publicMarketDataStatusName(diagnostic.lastStatus);
        out += " stream=";
        out += marketStreamName(diagnostic.primaryStream);
        if (diagnostic.firstSymbol.data[0] != '\0') {
            out += " symbol=";
            out += diagnostic.firstSymbol.data;
        }
        if (diagnostic.endpointHost && diagnostic.endpointHost[0] != '\0') {
            out += " endpoint=wss://";
            out += diagnostic.endpointHost;
            out += ":";
            out += std::to_string(diagnostic.endpointPort);
            out += diagnostic.connectPath;
        }
        if (diagnostic.lastWsConnectError != 0) {
            out += " ws_error=";
            out += std::to_string(diagnostic.lastWsConnectError);
        }
        const std::string_view connectStage{diagnostic.lastWsConnectStage};
        if (!connectStage.empty() && connectStage != std::string_view{"none"}) {
            out += " connect_stage=";
            out += diagnostic.lastWsConnectStage;
        }
        if (diagnostic.lastWsConnectDetail[0] != '\0') {
            out += " connect_detail=";
            out += diagnostic.lastWsConnectDetail;
        }
        if (diagnostic.lastRouteError[0] != '\0') {
            out += " detail=";
            out += diagnostic.lastRouteError;
        }
        out += " frames=";
        out += std::to_string(diagnostic.frames);
        out += " parsed=";
        out += std::to_string(diagnostic.parsedFrames);
        out += " parse_failures=";
        out += std::to_string(diagnostic.parseFailures);
        if (diagnostic.lastFrameSnippet[0] != '\0') {
            out += " last_frame=";
            out += diagnostic.lastFrameSnippet;
        }
    }
    return out;
}

void pollMarketDataLifecycleIfDue(hft_trader::runtime::MarketDataRuntime& runtime,
                                  std::int64_t& nextPollNs,
                                  std::string* diagnosticOut = nullptr,
                                  std::string_view scope = {}) {
    const auto nowNs = internal::nowNs();
    if (nowNs < nextPollNs) return;
    nextPollNs = nowNs + kMarketDataLifecyclePollIntervalNs;
    const std::size_t progressed = runtime.pollLifecycleOnce();
    if (diagnosticOut != nullptr && progressed != 0u) {
        *diagnosticOut = marketDataRuntimeDiagnosticText(runtime, scope);
    }
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

bool detailedCandlesNeedInstrumentMetadata(const CaptureConfig& config) noexcept {
    return textEqualsAscii(config.exchange, "finam")
        && !textEqualsAscii(config.market, "spot")
        && !textEqualsAscii(config.market, "shares");
}

ExchangeId exchangeIdFromConfig(std::string_view exchange) noexcept {
    if (textEqualsAscii(exchange, "binance")) return canon::kExchangeIdBinance;
    if (textEqualsAscii(exchange, "bybit")) return canon::kExchangeIdBybit;
    if (textEqualsAscii(exchange, "kucoin")) return canon::kExchangeIdKucoin;
    if (textEqualsAscii(exchange, "gate")) return canon::kExchangeIdGate;
    if (textEqualsAscii(exchange, "bitget")) return canon::kExchangeIdBitget;
    if (textEqualsAscii(exchange, "aster")) return canon::kExchangeIdAster;
    if (textEqualsAscii(exchange, "hyperliquid")) return canon::kExchangeIdHyperliquid;
    if (textEqualsAscii(exchange, "okx")) return canon::kExchangeIdOkx;
    if (textEqualsAscii(exchange, "finam")) return canon::kExchangeIdFinam;
    if (textEqualsAscii(exchange, "mexc")) return canon::kExchangeIdMexc;
    if (textEqualsAscii(exchange, "xt")) return canon::kExchangeIdXt;
    if (textEqualsAscii(exchange, "bingx")) return canon::kExchangeIdBingx;
    if (textEqualsAscii(exchange, "bitmart")) return canon::kExchangeIdBitmart;
    if (textEqualsAscii(exchange, "toobit")) return canon::kExchangeIdToobit;
    if (textEqualsAscii(exchange, "htx")) return canon::kExchangeIdHtx;
    if (textEqualsAscii(exchange, "phemex")) return canon::kExchangeIdPhemex;
    return canon::kExchangeIdUnknown;
}

canon::MarketType marketTypeFromConfig(ExchangeId exchange, std::string_view market) noexcept {
    char apiMarket[64]{};
    if (market.size() + 1u < sizeof(apiMarket)) {
        for (std::size_t i = 0u; i < market.size(); ++i) {
            char c = market[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            apiMarket[i] = c;
        }
        const canon::MarketType mapped = canon::exchangeApiStringToCanonical(exchange, apiMarket);
        if (mapped.raw != canon::kMarketTypeUnknown.raw) return mapped;
    }
    if (textEqualsAscii(market, "spot") || textEqualsAscii(market, "shares")) return canon::kMarketTypeSpot;
    if (textEqualsAscii(market, "margin")) return canon::kMarketTypeMargin;
    if (textEqualsAscii(market, "inverse")) return canon::kMarketTypeInverse;
    if (textEqualsAscii(market, "swap")) return canon::kMarketTypeSwap;
    if (textEqualsAscii(market, "futures") ||
        textEqualsAscii(market, "forts") ||
        textEqualsAscii(market, "futures_usd")) return canon::kMarketTypeFutures;
    return canon::kMarketTypeUnknown;
}

bool shouldFetchInitialOrderbookSnapshot(const CaptureConfig& config) noexcept {
    return !textEqualsAscii(config.exchange, "bitget");
}

hft_trader::runtime::VenueRuntimeConfig makeTraderVenueConfig(const CaptureConfig& config) {
    hft_trader::runtime::VenueRuntimeConfig venue{};
    venue.name = config.exchange + "." + config.market;
    venue.exchange = exchangeIdFromConfig(config.exchange);
    venue.market = marketTypeFromConfig(venue.exchange, config.market);
    venue.apiSlot = internal::normalizedApiSlot(config);
    venue.hasApiSlot = true;
    venue.marketEnabled = true;
    venue.userEnabled = false;
    venue.orderEnabled = false;
    venue.controlEnabled = false;
    const std::string_view routeSymbolText = internal::primaryRouteSymbolText(config);
    if (!routeSymbolText.empty()) {
        Symbol symbol{};
        copySymbolFromText(symbol, routeSymbolText);
        if (symbol.data[0] != '\0') venue.symbols.push_back(symbol);
    }
    hft_trader::runtime::setStrategyParam(venue.params, "reference_poll_interval_ms", "5000");
    return venue;
}

bool validDetailedCandle(const cxet::composite::Ohlcv& candle) noexcept {
    return candle.ts.raw > 0 &&
           candle.open.raw > 0 &&
           candle.high.raw > 0 &&
           candle.low.raw > 0 &&
           candle.close.raw > 0 &&
           candle.high.raw >= candle.low.raw;
}

std::size_t validDetailedCandleCount(const std::vector<cxet::composite::Ohlcv>& rows,
                                     std::size_t rowCount) noexcept {
    std::size_t count = 0u;
    const std::size_t capped = std::min(rowCount, rows.size());
    for (std::size_t i = 0u; i < capped; ++i) {
        if (validDetailedCandle(rows[i])) ++count;
    }
    return count;
}

bool fetchDetailedCandlesRows(const CaptureConfig& config,
                              std::vector<cxet::composite::Ohlcv>& rows,
                              std::size_t& rowCount,
                              std::string& tfText,
                              std::string& errorText) noexcept {
    rowCount = 0u;
    rows.clear();
    errorText.clear();

    if (internal::primaryRouteSymbolText(config).empty()) {
        errorText = "candles2: missing symbol";
        return false;
    }

    Symbol symbol{};
    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
    if (symbol.data[0] == '\0') {
        errorText = "candles2: invalid symbol";
        return false;
    }

    TimeframeBuf timeframe{};
    tfText = config.detailedCandlesTimeframe.empty() ? std::string{"15m"} : config.detailedCandlesTimeframe;
    timeframe.copyFrom(tfText.c_str());
    if (timeframe.data[0] == '\0') {
        errorText = "candles2: invalid timeframe";
        return false;
    }

    CountVal limit{};
    const std::uint32_t requestedLimit = config.detailedCandlesLimit == 0u
        ? kDetailedCandlesDefaultLimit
        : config.detailedCandlesLimit;
    limit.raw = std::min<std::uint32_t>(requestedLimit, kDetailedCandlesMaxLimit);

    TimeNs endTime{};
    endTime.raw = config.detailedCandlesEndNs > 0
        ? static_cast<std::uint64_t>(config.detailedCandlesEndNs)
        : static_cast<std::uint64_t>(internal::nowNs());

    rows.resize(static_cast<std::size_t>(limit.raw));
    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    std::string fetchFailure;
    const bool fetched = hft_trader::runtime::candles::loadOhlcvHistoryForVenue(
        makeTraderVenueConfig(config),
        symbol,
        timeframe,
        limit,
        endTime,
        rows.data(),
        rows.size(),
        &rowCount,
        requestBuf,
        recvBuf,
        &fetchFailure);
    if (!fetched) {
        errorText = "candles2: trader OHLCV fetch failed exchange=" + config.exchange +
                    " market=" + config.market +
                    " symbol=" + config.symbols.front() +
                    " timeframe=" + tfText;
        if (!fetchFailure.empty()) errorText += " reason=" + fetchFailure;
        if (requestBuf.size() != 0u) {
            errorText += " request=";
            errorText.append(requestBuf.data(), std::min<std::size_t>(requestBuf.size(), 240u));
        }
        errorText += " response_bytes=" + std::to_string(recvBuf.size());
        return false;
    }
    if (validDetailedCandleCount(rows, rowCount) == 0u) {
        errorText = "candles2: fetch returned no valid OHLCV rows exchange=" + config.exchange +
                    " market=" + config.market +
                    " symbol=" + config.symbols.front() +
                    " timeframe=" + tfText +
                    " parsed_rows=" + std::to_string(rowCount);
        return false;
    }
    return true;
}

Status detailedCandlesFetchStatus(std::string_view errorText) noexcept {
    if (errorText == "candles2: missing symbol" ||
        errorText == "candles2: invalid symbol" ||
        errorText == "candles2: invalid timeframe") {
        return Status::InvalidArgument;
    }
    return Status::Unknown;
}

hft_trader::runtime::HftRuntimeConfig makeTraderMarketDataConfig(
    const CaptureConfig& config,
    Span<const cxet::api::market::PublicMarketDataStream> streams) {
    hft_trader::runtime::HftRuntimeConfig cfg{};
    cfg.name = "hft_recorder_market_data_client";
    cfg.strategyType = "explicit_inputs";
    cfg.hasInputs = true;
    cfg.inputs.assign(streams.data(), streams.data() + streams.size());
    cfg.venues.push_back(makeTraderVenueConfig(config));
    return cfg;
}

bool applyTraderMarketDataConfig(hft_trader::runtime::MarketDataRuntime& runtime,
                                 const CaptureConfig& config,
                                 Span<const cxet::api::market::PublicMarketDataStream> streams,
                                 std::string& err) noexcept {
    const auto cfg = makeTraderMarketDataConfig(config, streams);
    if (cfg.venues.empty() || cfg.venues.front().symbols.empty()) {
        err = "empty recorder market-data symbol";
        return false;
    }
    return runtime.applyConfig(cfg, err);
}

cxet::composite::StreamObjectType streamObjectTypeForRecorder(
    cxet::api::market::PublicMarketDataStream stream) noexcept {
    switch (stream) {
        case cxet::api::market::PublicMarketDataStream::Trades: return cxet::composite::StreamObjectType::Trade;
        case cxet::api::market::PublicMarketDataStream::Orderbook: return cxet::composite::StreamObjectType::Orderbook;
        case cxet::api::market::PublicMarketDataStream::Liquidations: return cxet::composite::StreamObjectType::Liquidation;
        case cxet::api::market::PublicMarketDataStream::PriceLimit: return cxet::composite::StreamObjectType::PriceLimit;
        case cxet::api::market::PublicMarketDataStream::MarkPrice: return cxet::composite::StreamObjectType::MarkPrice;
        case cxet::api::market::PublicMarketDataStream::IndexPrice: return cxet::composite::StreamObjectType::MarkPrice;
        case cxet::api::market::PublicMarketDataStream::Funding: return cxet::composite::StreamObjectType::Funding;
        case cxet::api::market::PublicMarketDataStream::OpenInterest: return cxet::composite::StreamObjectType::OpenInterest;
        case cxet::api::market::PublicMarketDataStream::BookTicker: return cxet::composite::StreamObjectType::BookTicker;
    }
    return cxet::composite::StreamObjectType::BookTicker;
}

const char* referenceStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept {
    switch (stream) {
        case cxet::api::market::PublicMarketDataStream::PriceLimit: return "price_limit";
        case cxet::api::market::PublicMarketDataStream::MarkPrice: return "mark_price";
        case cxet::api::market::PublicMarketDataStream::IndexPrice: return "index_price";
        case cxet::api::market::PublicMarketDataStream::Funding: return "funding";
        default: return "unknown";
    }
}

const char* marketStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept {
    switch (stream) {
        case cxet::api::market::PublicMarketDataStream::Trades: return "trades";
        case cxet::api::market::PublicMarketDataStream::BookTicker: return "bookticker";
        case cxet::api::market::PublicMarketDataStream::Orderbook: return "orderbook";
        case cxet::api::market::PublicMarketDataStream::Liquidations: return "liquidations";
        case cxet::api::market::PublicMarketDataStream::PriceLimit: return "price_limit";
        case cxet::api::market::PublicMarketDataStream::MarkPrice: return "mark_price";
        case cxet::api::market::PublicMarketDataStream::IndexPrice: return "index_price";
        case cxet::api::market::PublicMarketDataStream::Funding: return "funding";
        case cxet::api::market::PublicMarketDataStream::OpenInterest: return "open_interest";
    }
    return "unknown";
}

cxet::composite::StreamMeta streamMetaFromTraderEvent(
    const hft_trader::runtime::MarketDataRuntimeEvent& event,
    std::string_view identitySymbol) noexcept {
    cxet::composite::StreamMeta meta{};
    if (event.channel) {
        meta.exchangeId = event.channel->exchange;
        meta.market = event.channel->market;
        meta.symbol = event.channel->symbol;
    }
    if (!identitySymbol.empty()) copySymbolFromText(meta.symbol, identitySymbol);
    meta.objectType = streamObjectTypeForRecorder(event.stream);
    return meta;
}

const char* candleTierStatusName(cxet::composite::CandleHistoryTierStatus status) noexcept {
    switch (status) {
        case cxet::composite::CandleHistoryTierStatus::Ok: return "Ok";
        case cxet::composite::CandleHistoryTierStatus::NotRequested: return "NotRequested";
        case cxet::composite::CandleHistoryTierStatus::BadConfig: return "BadConfig";
        case cxet::composite::CandleHistoryTierStatus::FetchFailed: return "FetchFailed";
        case cxet::composite::CandleHistoryTierStatus::ParseFailed: return "ParseFailed";
        case cxet::composite::CandleHistoryTierStatus::Empty: return "Empty";
    }
    return "Unknown";
}

std::string candleHistoryStatusText(const cxet::composite::TieredCandleHistory& history) {
    return "m1=" + std::string(candleTierStatusName(history.m1Status)) + " count=" + std::to_string(history.m1Count.raw) +
           ", m15=" + std::string(candleTierStatusName(history.m15Status)) + " count=" + std::to_string(history.m15Count.raw) +
           ", d1=" + std::string(candleTierStatusName(history.d1Status)) + " count=" + std::to_string(history.d1Count.raw);
}

}  // namespace


Status CaptureCoordinator::captureCandlesOnce(const CaptureConfig& config) noexcept {
    if (internal::primaryRouteSymbolText(config).empty() || sessionDir_.empty()) return Status::InvalidArgument;
    if (candlesCount_.load(std::memory_order_acquire) != 0u) return Status::Ok;

    Symbol symbol{};
    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
    if (symbol.data[0] == '\0') return Status::InvalidArgument;

    cxet::composite::TieredCandleHistory history{};
    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    const bool fetched = hft_trader::runtime::candles::loadTieredCandleHistoryForVenue(
        makeTraderVenueConfig(config),
        symbol,
        history,
        requestBuf,
        recvBuf);
    if (!fetched) {
        lastError_ = "candles: trader history fetch failed (" + candleHistoryStatusText(history) + ")";
        return Status::Unknown;
    }

    if (!isOk(candlesWriter_.open(ChannelKind::Candles, sessionDir_))) {
        if (lastError_.empty()) lastError_ = "candles: failed to create candles.jsonl";
        return Status::IoError;
    }

    auto appendTier = [&](std::int64_t tier,
                          const cxet::composite::CandleLite* rows,
                          std::uint32_t count) noexcept -> Status {
        for (std::uint32_t i = 0u; i < count && i < cxet::composite::kTieredCandleHistoryCapacity; ++i) {
            const auto& candle = rows[i];
            replay::CandleRow row{};
            row.tier = tier;
            row.tsNs = static_cast<std::int64_t>(candle.ts.raw);
            row.highE8 = static_cast<std::int64_t>(candle.high.raw);
            row.lowE8 = static_cast<std::int64_t>(candle.low.raw);
            row.quoteAmountE8 = static_cast<std::int64_t>(candle.quoteAmount.raw);
            if (row.tsNs <= 0 || row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) continue;
            const auto line = renderCandleJsonLine(row);
            const auto writeStatus = candlesWriter_.writeLine(line);
            if (!isOk(writeStatus)) return writeStatus;
            candlesCount_.fetch_add(1, std::memory_order_acq_rel);
        }
        return Status::Ok;
    };

    Status status = Status::Ok;
    if (isOk(status)) status = appendTier(1, history.m1, history.m1Count.raw);
    if (isOk(status)) status = appendTier(2, history.m15, history.m15Count.raw);
    if (isOk(status)) status = appendTier(3, history.d1, history.d1Count.raw);
    if (!isOk(status)) {
        if (lastError_.empty()) lastError_ = "candles: failed to write candles.jsonl";
        return status;
    }

    if (candlesCount_.load(std::memory_order_acquire) != 0u) {
        if (lastError_.starts_with("candles:")) lastError_.clear();
        manifest_.candlesEnabled = true;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.candlesPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.candlesPath);
        }
    }
    return Status::Ok;
}

Status CaptureCoordinator::probeDetailedCandlesOnce(const CaptureConfig& config) noexcept {
    internal::ensureCxetInitialized();
    if (const auto envStatus = internal::loadCaptureEnv(config, lastError_); !isOk(envStatus)) {
        return envStatus;
    }
    if (const auto validateStatus = internal::validateSupportedConfig(config, lastError_); !isOk(validateStatus)) {
        return validateStatus;
    }

    std::vector<cxet::composite::Ohlcv> rows;
    std::size_t rowCount = 0u;
    std::string tfText;
    std::string errorText;
    if (!fetchDetailedCandlesRows(config, rows, rowCount, tfText, errorText)) {
        lastError_ = errorText;
        return detailedCandlesFetchStatus(errorText);
    }
    if (lastError_.starts_with("candles2:")) lastError_.clear();
    return Status::Ok;
}

Status CaptureCoordinator::captureDetailedCandlesOnce(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;
    if (detailedCandlesNeedInstrumentMetadata(config) && !instrumentMetadataReady_) {
        lastError_ = "candles2: finam futures instrument metadata is required for price-basis logic; "
                     "refresh Finam auth and retry so /v1/assets/<symbol> can provide price_basis_qty_e8";
        return Status::Unknown;
    }

    std::vector<cxet::composite::Ohlcv> rows;
    std::size_t rowCount = 0u;
    std::string tfText;
    std::string errorText;
    if (!fetchDetailedCandlesRows(config, rows, rowCount, tfText, errorText)) {
        lastError_ = errorText;
        return detailedCandlesFetchStatus(errorText);
    }

    const std::string candles2Path = detailedCandlesRelativePath(tfText, true);
    const std::string candlesPath = detailedCandlesRelativePath(tfText, false);
    candles2Writer_.close();
    candlesWriter_.close();
    if (!isOk(candles2Writer_.openRelativePath(sessionDir_, candles2Path, true))) {
        if (lastError_.empty()) lastError_ = "candles2: failed to create candles2.jsonl";
        return Status::IoError;
    }

    const std::int64_t candleTier = candleTierFromTimeframe(tfText);
    const bool writeLegacyCandles = candleTier >= 1 && candleTier <= 3;
    if (writeLegacyCandles && !isOk(candlesWriter_.openRelativePath(sessionDir_, candlesPath, true))) {
        lastError_ = "candles2: failed to create compatibility candles.jsonl";
        return Status::IoError;
    }

    std::uint64_t written = 0u;
    std::uint64_t legacyWritten = 0u;
    for (std::size_t i = 0u; i < rowCount; ++i) {
        const auto& candle = rows[i];
        replay::CandleRow row{};
        row.tier = candleTier;
        row.exchange = config.exchange;
        row.market = config.market;
        row.symbol = std::string{internal::primaryIdentitySymbolText(config)};
        row.timeframe = tfText;
        row.tsNs = static_cast<std::int64_t>(candle.ts.raw);
        row.openE8 = static_cast<std::int64_t>(candle.open.raw);
        row.highE8 = static_cast<std::int64_t>(candle.high.raw);
        row.lowE8 = static_cast<std::int64_t>(candle.low.raw);
        row.closeE8 = static_cast<std::int64_t>(candle.close.raw);
        row.volumeE8 = static_cast<std::int64_t>(candle.amount.raw);
        row.quoteAmountE8 = static_cast<std::int64_t>(candle.quoteAmount.raw);
        row.hasOhlc = true;
        if (!validDetailedCandle(candle) || row.volumeE8 < 0 || row.quoteAmountE8 < 0) {
            continue;
        }
        const auto line = renderCandleJsonLine(row);
        const auto writeStatus = candles2Writer_.writeLine(line);
        if (!isOk(writeStatus)) {
            lastError_ = "candles2: failed to write candles2.jsonl";
            return writeStatus;
        }
        candles2Count_.fetch_add(1, std::memory_order_acq_rel);
        ++written;

        if (writeLegacyCandles) {
            replay::CandleRow lite{};
            lite.tier = candleTier;
            lite.tsNs = row.tsNs;
            lite.highE8 = row.highE8;
            lite.lowE8 = row.lowE8;
            lite.quoteAmountE8 = row.quoteAmountE8;
            const auto legacyLine = renderCandleJsonLine(lite);
            const auto legacyWriteStatus = candlesWriter_.writeLine(legacyLine);
            if (!isOk(legacyWriteStatus)) {
                lastError_ = "candles2: failed to write compatibility candles.jsonl";
                return legacyWriteStatus;
            }
            candlesCount_.fetch_add(1, std::memory_order_acq_rel);
            ++legacyWritten;
        }
    }

    if (written == 0u) {
        lastError_ = "candles2: fetch returned no valid OHLCV rows exchange=" + config.exchange +
                     " market=" + config.market +
                     " symbol=" + config.symbols.front() +
                     " timeframe=" + tfText +
                     " parsed_rows=" + std::to_string(rowCount);
        return Status::Unknown;
    }

    if (lastError_.starts_with("candles2:")) lastError_.clear();
    manifest_.candles2Enabled = true;
    manifest_.candles2Path = candles2Path;
    manifest_.candles2Count = candles2Count_.load(std::memory_order_relaxed);
    if (legacyWritten != 0u) {
        manifest_.candlesEnabled = true;
        manifest_.candlesPath = candlesPath;
        manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.candlesPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.candlesPath);
        }
    }
    if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.candles2Path)
        == manifest_.canonicalArtifacts.end()) {
        manifest_.canonicalArtifacts.push_back(manifest_.candles2Path);
    }
    return writeManifestFile_();
}

Status CaptureCoordinator::captureTradesHistoryOnce(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;
    if (config.symbols.empty()) {
        lastError_ = "trades_history: missing symbol";
        return Status::InvalidArgument;
    }
    if (config.tradesHistoryWarmupSec <= 0) {
        lastError_ = "trades_history: history window must be > 0 seconds";
        return Status::InvalidArgument;
    }

    Symbol symbol{};
    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
    if (symbol.data[0] == '\0') {
        lastError_ = "trades_history: invalid symbol";
        return Status::InvalidArgument;
    }

    const auto warmupSec = std::min<std::int64_t>(config.tradesHistoryWarmupSec, kTradesHistoryWarmupMaxSec);
    const auto warmupNs = warmupSec * 1000000000LL;
    const auto endNs = config.tradesHistoryEndNs > 0 ? config.tradesHistoryEndNs : internal::nowNs();
    const auto startNs = endNs > warmupNs ? endNs - warmupNs : 0;
    CountVal pageLimit{};
    pageLimit.raw = config.tradesHistoryPageLimit == 0u ? kTradesHistoryWarmupPageLimit : config.tradesHistoryPageLimit;

    TradesHistoryWarmupState history{};
    history.requestedStartNs = startNs;
    history.requestedEndNs = endNs;
    TradesHistorySinkContext sinkContext{};
    sinkContext.state = &history;
    sinkContext.tradesCaptureSeq = &tradesCaptureSeq_;
    sinkContext.ingestSeq = &ingestSeq_;
    sinkContext.exchange = config.exchange;
    sinkContext.market = config.market;
    sinkContext.identitySymbol = std::string{internal::primaryIdentitySymbolText(config)};
    sinkContext.maxRows = config.tradesHistoryMaxRows;

    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    cxet::api::trades::HistoricalTradesResult result{};
    TimeNs startTime{};
    TimeNs endTime{};
    startTime.raw = static_cast<std::uint64_t>(startNs > 0 ? startNs : 0);
    endTime.raw = static_cast<std::uint64_t>(endNs > 0 ? endNs : 0);
    const bool fetched = hft_trader::runtime::trades::loadPublicTradeHistoryForVenue(
        makeTraderVenueConfig(config),
        symbol,
        startTime,
        endTime,
        pageLimit,
        true,
        appendHistoricalTradesToWarmup,
        &sinkContext,
        result,
        requestBuf,
        recvBuf);
    if (sinkContext.hitRowLimit) {
        result.rowsTotal.raw = static_cast<std::uint64_t>(history.historyRows.size());
        result.status = cxet::api::trades::HistoricalTradesStatus::Ok;
    }

    std::vector<replay::TradeRow> historyRows;
    {
        std::lock_guard<std::mutex> lock(history.mutex);
        historyRows = std::move(history.historyRows);
    }
    std::sort(historyRows.begin(), historyRows.end(), tradeLessByEventTime);
    historyRows.erase(std::unique(historyRows.begin(), historyRows.end(), sameTradeEvent), historyRows.end());

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.tradesEnabled = true;
        manifest_.tradesHistoryWarmupSec = warmupSec;
        manifest_.tradesHistoryRequestedStartNs = startNs;
        manifest_.tradesHistoryRequestedEndNs = endNs;
        manifest_.tradesHistoryRows = static_cast<std::uint64_t>(historyRows.size());
        manifest_.tradesHistoryRequests = static_cast<std::uint64_t>(result.requestsTotal.raw);
        manifest_.tradesHistoryFeedKind = historicalTradeFeedKindName(result.feedKind);
        manifest_.tradesHistoryStatus = historicalTradesStatusName(result.status);
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.tradesPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.tradesPath);
        }
    }
    if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Trades))) {
        lastError_ = "trades_history: failed to create trades.jsonl";
        return Status::IoError;
    }

    for (const auto& row : historyRows) {
        const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
        const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
        if (!isOk(fileStatus)) {
            lastError_ = "trades_history: failed to write trades.jsonl";
            return fileStatus;
        }
        (void)appendLiveTrade(row);
        tradesCount_.fetch_add(1, std::memory_order_acq_rel);
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.tradesCount = tradesCount_.load(std::memory_order_relaxed);
    }

    if (!fetched) {
        lastError_ = std::string{"trades_history: REST fetch failed status="}
            + historicalTradesStatusName(result.status);
        if (requestBuf.size() != 0u) {
            lastError_ += " request=";
            lastError_.append(requestBuf.data(), std::min<std::size_t>(requestBuf.size(), 240u));
        }
        lastError_ += " response_bytes=" + std::to_string(recvBuf.size());
        (void)writeManifestFile_();
        return Status::Unknown;
    }
    if (historyRows.empty()) {
        lastError_ = std::string{"trades_history: no valid rows status="}
            + historicalTradesStatusName(result.status);
        (void)writeManifestFile_();
        return Status::Unknown;
    }

    if (lastError_.starts_with("trades_history:")) lastError_.clear();
    return writeManifestFile_();
}

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
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.depthSidecarPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.depthSidecarPath);
                }
                if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::DepthTape))
                    || !isOk(jsonSink_.ensureChannelFile(ChannelKind::DepthSidecar))) {
                    lastError_ = "failed to create depth_tape/depth_sidecar jsonl files";
                    return Status::IoError;
                }
            }
            desiredOrderbook_.store(true, std::memory_order_release);
            orderbookRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::MarkPrice: {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.markPriceEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.markPricePath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.markPricePath);
                }
            }
            if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::MarkPrice))) {
                lastError_ = "failed to create mark_price.jsonl";
                return Status::IoError;
            }
            desiredMarkPrice_.store(true, std::memory_order_release);
            markPriceRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::IndexPrice: {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.indexPriceEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.indexPricePath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.indexPricePath);
                }
            }
            if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::IndexPrice))) {
                lastError_ = "failed to create index_price.jsonl";
                return Status::IoError;
            }
            desiredIndexPrice_.store(true, std::memory_order_release);
            indexPriceRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::Funding: {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.fundingEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.fundingPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.fundingPath);
                }
            }
            if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Funding))) {
                lastError_ = "failed to create funding.jsonl";
                return Status::IoError;
            }
            desiredFunding_.store(true, std::memory_order_release);
            fundingRunning_.store(true, std::memory_order_release);
            break;
        }
        case ManagedStreamKind::PriceLimit: {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                manifest_.priceLimitEnabled = true;
                if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.priceLimitPath)
                    == manifest_.canonicalArtifacts.end()) {
                    manifest_.canonicalArtifacts.push_back(manifest_.priceLimitPath);
                }
            }
            if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::PriceLimit))) {
                lastError_ = "failed to create price_limit.jsonl";
                return Status::IoError;
            }
            desiredPriceLimit_.store(true, std::memory_order_release);
            priceLimitRunning_.store(true, std::memory_order_release);
            break;
        }
    }

    const bool referenceStream = stream == ManagedStreamKind::MarkPrice
        || stream == ManagedStreamKind::IndexPrice
        || stream == ManagedStreamKind::Funding
        || stream == ManagedStreamKind::PriceLimit;
    if (referenceStream) {
        if (referenceDataThread_.joinable() && !referenceDataRunning_.load(std::memory_order_acquire)) {
            referenceDataThread_.join();
        }
        if (!referenceDataThread_.joinable()) {
            referenceDataStop_.store(false, std::memory_order_release);
            referenceDataRunning_.store(true, std::memory_order_release);
            referenceDataThread_ = std::thread([this, normalizedConfig]() mutable noexcept {
                referenceDataManagerLoop_(normalizedConfig);
            });
        }
        return Status::Ok;
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

    auto subscribeBuilder = internal::makeLiquidationBuilder(config);
    if (!internal::applyRequestedAliases(config.liquidationAliases, subscribeBuilder, lastError_)) {
        return Status::InvalidArgument;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.liquidationsEnabled = true;
        manifest_.liquidationsRequiredWhenEnabled = false;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.liquidationsPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.liquidationsPath);
        }
        if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Liquidations))) {
            lastError_ = "failed to create liquidations.jsonl";
            return Status::IoError;
        }
    }
    liquidationsStop_.store(false, std::memory_order_release);
    liquidationsRunning_.store(true, std::memory_order_release);
    if (liquidationsThread_.joinable()) liquidationsThread_.join();
    liquidationsThread_ = std::thread([this, config]() noexcept {
        liquidationsLoop_(config);
    });
    return Status::Ok;
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

Status CaptureCoordinator::startMarkPrice(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::MarkPrice);
}

Status CaptureCoordinator::requestStopMarkPrice() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::MarkPrice);
    return Status::Ok;
}

Status CaptureCoordinator::stopMarkPrice() noexcept {
    (void)requestStopMarkPrice();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

Status CaptureCoordinator::startIndexPrice(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::IndexPrice);
}

Status CaptureCoordinator::requestStopIndexPrice() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::IndexPrice);
    return Status::Ok;
}

Status CaptureCoordinator::stopIndexPrice() noexcept {
    (void)requestStopIndexPrice();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

Status CaptureCoordinator::startFunding(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::Funding);
}

Status CaptureCoordinator::requestStopFunding() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::Funding);
    return Status::Ok;
}

Status CaptureCoordinator::stopFunding() noexcept {
    (void)requestStopFunding();
    joinManagedMarketDataIfIdle_();
    return Status::Ok;
}

Status CaptureCoordinator::startPriceLimit(const CaptureConfig& config) noexcept {
    return startManagedMarketData_(config, ManagedStreamKind::PriceLimit);
}

Status CaptureCoordinator::requestStopPriceLimit() noexcept {
    requestStopManagedMarketData_(ManagedStreamKind::PriceLimit);
    return Status::Ok;
}

Status CaptureCoordinator::stopPriceLimit() noexcept {
    (void)requestStopPriceLimit();
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
        case ManagedStreamKind::MarkPrice:
            desiredMarkPrice_.store(false, std::memory_order_release);
            markPriceRunning_.store(false, std::memory_order_release);
            markPriceStop_.store(true, std::memory_order_release);
            break;
        case ManagedStreamKind::IndexPrice:
            desiredIndexPrice_.store(false, std::memory_order_release);
            indexPriceRunning_.store(false, std::memory_order_release);
            indexPriceStop_.store(true, std::memory_order_release);
            break;
        case ManagedStreamKind::Funding:
            desiredFunding_.store(false, std::memory_order_release);
            fundingRunning_.store(false, std::memory_order_release);
            fundingStop_.store(true, std::memory_order_release);
            break;
        case ManagedStreamKind::PriceLimit:
            desiredPriceLimit_.store(false, std::memory_order_release);
            priceLimitRunning_.store(false, std::memory_order_release);
            priceLimitStop_.store(true, std::memory_order_release);
            break;
    }
}

bool CaptureCoordinator::anyManagedMarketDataDesired_() const noexcept {
    return desiredTrades_.load(std::memory_order_acquire)
        || desiredBookTicker_.load(std::memory_order_acquire)
        || desiredOrderbook_.load(std::memory_order_acquire)
        || desiredMarkPrice_.load(std::memory_order_acquire)
        || desiredIndexPrice_.load(std::memory_order_acquire)
        || desiredFunding_.load(std::memory_order_acquire)
        || desiredPriceLimit_.load(std::memory_order_acquire);
}

void CaptureCoordinator::joinManagedMarketDataIfIdle_() noexcept {
    if (anyManagedMarketDataDesired_()) return;
    marketDataStop_.store(true, std::memory_order_release);
    if (marketDataThread_.joinable()) marketDataThread_.join();
    referenceDataStop_.store(true, std::memory_order_release);
    if (referenceDataThread_.joinable()) referenceDataThread_.join();
    marketDataRunning_.store(false, std::memory_order_release);
    referenceDataRunning_.store(false, std::memory_order_release);
}


void CaptureCoordinator::liquidationsLoop_(CaptureConfig config) noexcept {
    hft_trader::runtime::MarketDataRuntime traderMarket{};
    const cxet::api::market::PublicMarketDataStream streams[] = {
        cxet::api::market::PublicMarketDataStream::Liquidations,
    };
    std::string err;
    if (!applyTraderMarketDataConfig(traderMarket, config, Span<const cxet::api::market::PublicMarketDataStream>(streams, 1u), err)) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = err.empty() ? "liquidations: trader market-data apply failed" : err;
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }

    std::int64_t nextManifestFlushNs = internal::nowNs() + kRecordingManifestFlushIntervalNs;
    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!liquidationsStop_.load(std::memory_order_acquire)) {
        (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(traderMarket, nextLifecyclePollNs, &routeDiagnostic, "liquidations");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }
        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (traderMarket.pollAvailableOne(event)) {
            if (event.stream != cxet::api::market::PublicMarketDataStream::Liquidations ||
                event.status != cxet::api::market::PublicMarketDataStatus::Parsed) {
                continue;
            }
            const auto sequenceIds = nextEventSequenceIds(liquidationsCaptureSeq_, ingestSeq_);
            const auto capturedLiquidation = cxet_bridge::CxetCaptureBridge::captureLiquidation(event.liquidation);
            auto row = makeLiquidationRow(capturedLiquidation, config.exchange, config.market, sequenceIds);
            if (!internal::primaryIdentitySymbolText(config).empty()) {
                row.symbol = std::string{internal::primaryIdentitySymbolText(config)};
            }
            const auto jsonLine = renderLiquidationJsonLine(row, config.liquidationAliases);
            local_exchange::globalLocalMarketDataBus().publish("liquidations", row.symbol, jsonLine);
            const auto fileStatus = jsonSink_.appendLiquidationLine(row, jsonLine);
            if (!isOk(fileStatus)) {
                metrics::recordCaptureWriteError("liquidations");
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "liquidations: failed to write liquidations.jsonl";
                liquidationsStop_.store(true, std::memory_order_release);
                break;
            }
            (void)appendLiveLiquidation(row);
            liquidationsCount_.fetch_add(1, std::memory_order_acq_rel);
            metrics::recordCaptureEvent("liquidations",
                                        capturedLiquidation.tsNs,
                                        static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                        static_cast<std::uint64_t>(internal::nowNs()));
            continue;
        }
        (void)sleepCaptureStopAware(&liquidationsStop_, 1u);
    }
    nextManifestFlushNs = 0;
    (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
    traderMarket.closeAll();
    liquidationsRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::referenceDataManagerLoop_(CaptureConfig config) noexcept {
    hft_trader::runtime::MarketDataRuntime traderMarket{};
    std::uint8_t appliedMask = 0u;
    bool haveLastFunding = false;
    replay::FundingRow lastFunding{};

    auto desiredMask = [&]() noexcept -> std::uint8_t {
        std::uint8_t mask = 0u;
        if (desiredPriceLimit_.load(std::memory_order_acquire)) mask |= 1u;
        if (desiredMarkPrice_.load(std::memory_order_acquire)) mask |= 2u;
        if (desiredIndexPrice_.load(std::memory_order_acquire)) mask |= 4u;
        if (desiredFunding_.load(std::memory_order_acquire)) mask |= 8u;
        return mask;
    };

    auto rebuildDesired = [&]() -> bool {
        std::size_t count = 0u;
        std::array<cxet::api::market::PublicMarketDataStream, 4> streams{};
        const std::uint8_t mask = desiredMask();
        if ((mask & 1u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::PriceLimit;
        if ((mask & 2u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::MarkPrice;
        if ((mask & 4u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::IndexPrice;
        if ((mask & 8u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::Funding;

        std::string err;
        const Span<const cxet::api::market::PublicMarketDataStream> desiredStreams(streams.data(), count);
        if (applyTraderMarketDataConfig(traderMarket, config, desiredStreams, err)) return true;

        auto clearUnsupportedReferenceStream = [&](cxet::api::market::PublicMarketDataStream stream) noexcept {
            if (stream == cxet::api::market::PublicMarketDataStream::PriceLimit) {
                desiredPriceLimit_.store(false, std::memory_order_release);
                priceLimitRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::MarkPrice) {
                desiredMarkPrice_.store(false, std::memory_order_release);
                markPriceRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::IndexPrice) {
                desiredIndexPrice_.store(false, std::memory_order_release);
                indexPriceRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::Funding) {
                desiredFunding_.store(false, std::memory_order_release);
                fundingRunning_.store(false, std::memory_order_release);
            }
        };

        if (count <= 1u) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            if (count == 1u) clearUnsupportedReferenceStream(streams[0]);
            return false;
        }

        std::array<cxet::api::market::PublicMarketDataStream, 4> supported{};
        std::size_t supportedCount = 0u;
        std::string skipped;
        for (std::size_t i = 0u; i < count; ++i) {
            hft_trader::runtime::MarketDataRuntime probe{};
            std::string singleErr;
            const bool supportedStream = applyTraderMarketDataConfig(
                probe,
                config,
                Span<const cxet::api::market::PublicMarketDataStream>(&streams[i], 1u),
                singleErr);
            probe.closeAll();
            if (supportedStream) {
                supported[supportedCount++] = streams[i];
            } else {
                clearUnsupportedReferenceStream(streams[i]);
                if (!skipped.empty()) skipped += ", ";
                skipped += referenceStreamName(streams[i]);
            }
        }

        if (supportedCount == 0u) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            return false;
        }

        err.clear();
        if (!applyTraderMarketDataConfig(traderMarket,
                                         config,
                                         Span<const cxet::api::market::PublicMarketDataStream>(supported.data(), supportedCount),
                                         err)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            return false;
        }
        if (!skipped.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "reference market-data skipped unsupported stream(s): " + skipped;
        }
        return true;
    };

    auto writeMarkPrice = [&](const replay::MarkPriceRow& row) -> bool {
        const auto line = renderMarkPriceJsonLine(row);
        const auto fileStatus = jsonSink_.appendMarkPriceLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("mark_price");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "mark_price: failed to write mark_price.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveMarkPrice(row);
        markPriceCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("mark_price", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writeIndexPrice = [&](const replay::IndexPriceRow& row) -> bool {
        const auto line = renderIndexPriceJsonLine(row);
        const auto fileStatus = jsonSink_.appendIndexPriceLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("index_price");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "index_price: failed to write index_price.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveIndexPrice(row);
        indexPriceCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("index_price", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writeFunding = [&](const replay::FundingRow& row) -> bool {
        if (haveLastFunding && sameFundingTuple(lastFunding, row)) return true;
        const auto line = renderFundingJsonLine(row);
        const auto fileStatus = jsonSink_.appendFundingLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("funding");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "funding: failed to write funding.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveFunding(row);
        lastFunding = row;
        haveLastFunding = true;
        fundingCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("funding", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writePriceLimit = [&](const replay::PriceLimitRow& row) -> bool {
        const auto line = renderPriceLimitJsonLine(row);
        const auto fileStatus = jsonSink_.appendPriceLimitLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("price_limit");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "price_limit: failed to write price_limit.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLivePriceLimit(row);
        priceLimitCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("price_limit", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!referenceDataStop_.load(std::memory_order_acquire)) {
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
        }
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(traderMarket, nextLifecyclePollNs, &routeDiagnostic, "reference");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }

        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (!traderMarket.pollAvailableOne(event)) {
            (void)sleepCaptureStopAware(&referenceDataStop_, 1u);
            continue;
        }
        if (event.status != cxet::api::market::PublicMarketDataStatus::Parsed) continue;
        if (event.stream == cxet::api::market::PublicMarketDataStream::MarkPrice &&
            !writeMarkPrice(makeMarkPriceRow(event.markPrice))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::IndexPrice &&
            !writeIndexPrice(makeIndexPriceRow(event.indexPrice))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::Funding &&
            !writeFunding(makeFundingRow(event.funding))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::PriceLimit &&
            !writePriceLimit(makePriceLimitRow(event.priceLimit))) break;
    }

    traderMarket.closeAll();
    priceLimitRunning_.store(false, std::memory_order_release);
    markPriceRunning_.store(false, std::memory_order_release);
    indexPriceRunning_.store(false, std::memory_order_release);
    fundingRunning_.store(false, std::memory_order_release);
    referenceDataRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::marketDataManagerLoop_(CaptureConfig config) noexcept {
    hft_trader::runtime::MarketDataRuntime traderMarket{};
    std::uint8_t appliedMask = 0u;
    bool initialOrderbookSeedAttempted = depthCount_.load(std::memory_order_acquire) != 0u;
    std::vector<replay::PricePair> bitgetPreviousOrderbookLevels{};
    TradesHistoryWarmupState tradesWarmup{};
    std::thread tradesWarmupThread{};
    std::vector<replay::TradeRow> bufferedLiveTradeRows{};
    bool tradesWarmupFlushed = false;
    std::size_t tradesWarmupDisplayedRows = 0u;

    auto rebuildDesired = [&]() -> bool {
        const bool wantTrades = desiredTrades_.load(std::memory_order_acquire);
        const bool wantBookTicker = desiredBookTicker_.load(std::memory_order_acquire);
        const bool wantOrderbook = desiredOrderbook_.load(std::memory_order_acquire);
        std::array<cxet::api::market::PublicMarketDataStream, 3> streams{};
        std::size_t streamCount = 0u;
        if (wantTrades) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::Trades;
        if (wantBookTicker) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::BookTicker;
        if (wantOrderbook) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::Orderbook;

        std::string err;
        if (!applyTraderMarketDataConfig(traderMarket,
                                         config,
                                         Span<const cxet::api::market::PublicMarketDataStream>(streams.data(), streamCount),
                                         err)) {
            auto clearUnsupportedMarketStream = [&](cxet::api::market::PublicMarketDataStream stream) noexcept {
                if (stream == cxet::api::market::PublicMarketDataStream::Trades) {
                    desiredTrades_.store(false, std::memory_order_release);
                    tradesRunning_.store(false, std::memory_order_release);
                } else if (stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
                    desiredBookTicker_.store(false, std::memory_order_release);
                    bookTickerRunning_.store(false, std::memory_order_release);
                } else if (stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                    desiredOrderbook_.store(false, std::memory_order_release);
                    orderbookRunning_.store(false, std::memory_order_release);
                }
            };

            if (streamCount > 1u) {
                std::array<cxet::api::market::PublicMarketDataStream, 3> supported{};
                std::size_t supportedCount = 0u;
                std::string skipped;
                for (std::size_t i = 0u; i < streamCount; ++i) {
                    hft_trader::runtime::MarketDataRuntime probe{};
                    std::string singleErr;
                    const bool supportedStream = applyTraderMarketDataConfig(
                        probe,
                        config,
                        Span<const cxet::api::market::PublicMarketDataStream>(&streams[i], 1u),
                        singleErr);
                    probe.closeAll();
                    if (supportedStream) {
                        supported[supportedCount++] = streams[i];
                    } else {
                        clearUnsupportedMarketStream(streams[i]);
                        if (!skipped.empty()) skipped += ", ";
                        skipped += marketStreamName(streams[i]);
                        if (!singleErr.empty()) {
                            skipped += "(";
                            skipped += singleErr;
                            skipped += ")";
                        }
                    }
                }

                if (supportedCount != 0u) {
                    err.clear();
                    if (applyTraderMarketDataConfig(
                            traderMarket,
                            config,
                            Span<const cxet::api::market::PublicMarketDataStream>(supported.data(), supportedCount),
                            err)) {
                        if (!skipped.empty()) {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lastError_ = "market-data skipped unsupported stream(s): " + skipped;
                        }
                        return true;
                    }
                }
            }

            for (std::size_t i = 0u; i < streamCount; ++i) clearUnsupportedMarketStream(streams[i]);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "market-data: trader apply failed" : err;
            return false;
        }

        if (wantOrderbook) {
            if (!initialOrderbookSeedAttempted) {
                initialOrderbookSeedAttempted = true;
                cxet::composite::OrderBookSnapshot initialSnapshot{};
                if (!shouldFetchInitialOrderbookSnapshot(config)) {
                    // Bitget has no REST orderbook route in the trader loader here; use WS orderbook only.
                } else {
                    Symbol symbol{};
                    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
                    MessageBuffer requestBuf{};
                    MessageBuffer recvBuf{};
                    const bool snapshotOk = hft_trader::runtime::orderbook::loadOrderBookSnapshotForVenue(
                        makeTraderVenueConfig(config),
                        symbol,
                        initialSnapshot,
                        requestBuf,
                        recvBuf);
                    if (!snapshotOk) {
                        metrics::recordSnapshotFetchFailure("snapshot");
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        lastError_ = "orderbook: initial trader snapshot fetch failed; continuing with WS depth";
                    } else {
                        for (std::size_t i = 0u; i < traderMarket.channelCount(); ++i) {
                            const auto* channel = traderMarket.channelAt(i);
                            if (channel && channel->stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                                (void)traderMarket.seedOrderBookSnapshot(i, initialSnapshot);
                                break;
                            }
                        }
                        const auto capturedSnapshot = cxet_bridge::CxetCaptureBridge::captureOrderBook(initialSnapshot);
                        auto row = makeDepthRow(capturedSnapshot);
                        if (row.tsNs <= 0) row.tsNs = internal::nowNs();
                        const auto tapeLine = renderDepthTapeJsonLine(row);
                        const auto sidecarLine = renderDepthRleSidecarJsonLine(row);
                        const auto fileStatus = jsonSink_.appendDepthTapeSidecarLines(row, tapeLine, sidecarLine);
                        if (!isOk(fileStatus)) {
                            metrics::recordCaptureWriteError("depth");
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lastError_ = "orderbook: failed to write initial trader snapshot into depth_tape/depth_sidecar";
                            return false;
                        }
                        (void)appendLiveDepth(row);
                        depthCount_.fetch_add(1, std::memory_order_acq_rel);
                        metrics::recordCaptureEvent("depth", static_cast<std::uint64_t>(row.tsNs),
                                                    static_cast<std::uint64_t>(tapeLine.size() + sidecarLine.size() + 2u),
                                                    static_cast<std::uint64_t>(internal::nowNs()));
                    }
                }
            }
        }
        return true;
    };

    auto appendTradeRowToLiveOnly = [&](const replay::TradeRow& row, const std::string& jsonLine) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        local_exchange::globalLocalMarketDataBus().publish(kTradesText, row.symbol, jsonLine);
        const auto liveStatus = appendLiveTrade(row);
        if (!isOk(liveStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        tradesCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent(kTradesText,
                                    static_cast<std::uint64_t>(row.tsNs > 0 ? row.tsNs : 0),
                                    static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto appendTradeRowToLiveCacheOnly = [&](const replay::TradeRow& row) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        const auto liveStatus = appendLiveTrade(row);
        if (!isOk(liveStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
        tradesCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent(kTradesText,
                                    static_cast<std::uint64_t>(row.tsNs > 0 ? row.tsNs : 0),
                                    static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto appendTradeRowToFileOnly = [&](const replay::TradeRow& row) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
        const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    };

    auto startTradesWarmupIfNeeded = [&]() {
        if (tradesWarmup.started.load(std::memory_order_acquire)) return;
        if (!desiredTrades_.load(std::memory_order_acquire)) return;
        if (config.tradesHistoryWarmupSec <= 0) return;
        const auto warmupSec = std::min<std::int64_t>(config.tradesHistoryWarmupSec, kTradesHistoryWarmupMaxSec);
        const auto warmupNs = warmupSec * 1000000000LL;
        const auto endNs = internal::nowNs();
        const auto startNs = endNs > warmupNs ? endNs - warmupNs : 0;
        tradesWarmup.requestedStartNs = startNs;
        tradesWarmup.requestedEndNs = endNs;
        tradesWarmup.started.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            manifest_.tradesHistoryWarmupSec = warmupSec;
            manifest_.tradesHistoryRequestedStartNs = startNs;
            manifest_.tradesHistoryRequestedEndNs = endNs;
            manifest_.tradesHistoryStatus = "running";
        }
        tradesWarmupThread = std::thread([this, &tradesWarmup, config, startNs, endNs]() noexcept {
            Symbol symbol{};
            if (!internal::primaryRouteSymbolText(config).empty()) {
                copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
            }
            TimeNs startTime{};
            TimeNs endTime{};
            CountVal pageLimit{};
            startTime.raw = static_cast<std::uint64_t>(startNs > 0 ? startNs : 0);
            endTime.raw = static_cast<std::uint64_t>(endNs > 0 ? endNs : 0);
            pageLimit.raw = kTradesHistoryWarmupPageLimit;

            TradesHistorySinkContext sinkContext{};
            sinkContext.state = &tradesWarmup;
            sinkContext.tradesCaptureSeq = &tradesCaptureSeq_;
            sinkContext.ingestSeq = &ingestSeq_;
            sinkContext.exchange = config.exchange;
            sinkContext.market = config.market;
            sinkContext.identitySymbol = std::string{internal::primaryIdentitySymbolText(config)};
            sinkContext.maxRows = kTradesHistoryWarmupTargetRows;

            MessageBuffer requestBuf{};
            MessageBuffer recvBuf{};
            cxet::api::trades::HistoricalTradesResult result{};
            bool ok = hft_trader::runtime::trades::loadPublicTradeHistoryForVenue(
                makeTraderVenueConfig(config),
                symbol,
                startTime,
                endTime,
                pageLimit,
                true,
                appendHistoricalTradesToWarmup,
                &sinkContext,
                result,
                requestBuf,
                recvBuf);
            if (sinkContext.hitRowLimit) {
                std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
                result.rowsTotal.raw = static_cast<std::uint64_t>(tradesWarmup.historyRows.size());
                result.status = cxet::api::trades::HistoricalTradesStatus::Ok;
                ok = true;
            }
            tradesWarmup.result = result;
            if (!ok) {
                tradesWarmup.error = std::string{"trades history warmup failed: "}
                    + historicalTradesStatusName(result.status);
            }
            tradesWarmup.ok.store(ok, std::memory_order_release);
            tradesWarmup.done.store(true, std::memory_order_release);
        });
    };

    auto drainTradesWarmupPages = [&]() -> bool {
        if (!tradesWarmup.started.load(std::memory_order_acquire)) return true;
        if (tradesWarmupFlushed) return true;
        std::vector<replay::TradeRow> rows;
        {
            std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
            if (tradesWarmupDisplayedRows >= tradesWarmup.historyRows.size()) return true;
            rows.insert(rows.end(),
                        tradesWarmup.historyRows.begin() + static_cast<std::ptrdiff_t>(tradesWarmupDisplayedRows),
                        tradesWarmup.historyRows.end());
            tradesWarmupDisplayedRows = tradesWarmup.historyRows.size();
        }
        for (const auto& row : rows) {
            if (!appendTradeRowToLiveCacheOnly(row)) return false;
        }
        return true;
    };

    auto flushTradesWarmupIfReady = [&]() -> bool {
        if (tradesWarmupFlushed) return true;
        if (!tradesWarmup.started.load(std::memory_order_acquire)) return true;
        if (!tradesWarmup.done.load(std::memory_order_acquire)) return true;
        if (tradesWarmupThread.joinable()) tradesWarmupThread.join();
        if (!drainTradesWarmupPages()) return false;

        std::vector<replay::TradeRow> historyRows;
        {
            std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
            historyRows = std::move(tradesWarmup.historyRows);
        }
        struct PendingTradeRow {
            replay::TradeRow row{};
            bool publish{false};
        };
        std::vector<PendingTradeRow> pending;
        pending.reserve(historyRows.size() + bufferedLiveTradeRows.size());
        for (const auto& row : historyRows) pending.push_back(PendingTradeRow{row, false});
        for (const auto& row : bufferedLiveTradeRows) pending.push_back(PendingTradeRow{row, true});
        std::sort(pending.begin(), pending.end(), [](const PendingTradeRow& lhs, const PendingTradeRow& rhs) noexcept {
            if (tradeLessByEventTime(lhs.row, rhs.row)) return true;
            if (tradeLessByEventTime(rhs.row, lhs.row)) return false;
            return !lhs.publish && rhs.publish;
        });

        bool havePrevious = false;
        replay::TradeRow previous{};
        for (const auto& pendingRow : pending) {
            if (havePrevious && sameTradeEvent(previous, pendingRow.row)) {
                if (pendingRow.publish) tradesCount_.fetch_sub(1, std::memory_order_acq_rel);
                continue;
            }
            if (!appendTradeRowToFileOnly(pendingRow.row)) return false;
            previous = pendingRow.row;
            havePrevious = true;
        }
        bufferedLiveTradeRows.clear();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            manifest_.tradesHistoryWarmupSec = std::min<std::int64_t>(config.tradesHistoryWarmupSec, kTradesHistoryWarmupMaxSec);
            manifest_.tradesHistoryRequestedStartNs = tradesWarmup.requestedStartNs;
            manifest_.tradesHistoryRequestedEndNs = tradesWarmup.requestedEndNs;
            manifest_.tradesHistoryRows = static_cast<std::uint64_t>(historyRows.size());
            manifest_.tradesHistoryRequests = static_cast<std::uint64_t>(tradesWarmup.result.requestsTotal.raw);
            manifest_.tradesHistoryFeedKind = historicalTradeFeedKindName(tradesWarmup.result.feedKind);
            manifest_.tradesHistoryStatus = historicalTradesStatusName(tradesWarmup.result.status);
            if (!tradesWarmup.ok.load(std::memory_order_acquire)) {
                lastError_ = tradesWarmup.error;
            }
        }
        tradesWarmupFlushed = true;
        return true;
    };

    auto desiredMask = [&]() noexcept -> std::uint8_t {
        std::uint8_t mask = 0u;
        if (desiredTrades_.load(std::memory_order_acquire)) mask |= 1u;
        if (desiredBookTicker_.load(std::memory_order_acquire)) mask |= 2u;
        if (desiredOrderbook_.load(std::memory_order_acquire)) mask |= 4u;
        return mask;
    };

    std::int64_t nextManifestFlushNs = internal::nowNs() + kRecordingManifestFlushIntervalNs;
    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!marketDataStop_.load(std::memory_order_acquire)) {
        (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
            startTradesWarmupIfNeeded();
        }
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(traderMarket, nextLifecyclePollNs, &routeDiagnostic, "market-data");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;

        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (!traderMarket.pollAvailableOne(event)) {
            (void)sleepCaptureStopAware(&marketDataStop_, 1u);
            continue;
        }
        if (event.status != cxet::api::market::PublicMarketDataStatus::Parsed) continue;
        const bool captureMetrics = cxet::metrics::shouldCaptureLatency();
        const auto meta = streamMetaFromTraderEvent(event, internal::primaryIdentitySymbolText(config));
        if (event.stream == cxet::api::market::PublicMarketDataStream::Trades) {
                const auto sequenceIds = nextEventSequenceIds(tradesCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedTrade = cxet_bridge::CxetCaptureBridge::captureTrade(event.trade, meta);
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
                if (tradesWarmup.started.load(std::memory_order_acquire)
                    && !tradesWarmupFlushed) {
                    bufferedLiveTradeRows.push_back(row);
                    if (!appendTradeRowToLiveOnly(row, jsonLine)) break;
                    continue;
                }
                local_exchange::globalLocalMarketDataBus().publish("trades", row.symbol, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("trades");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "trades: failed to write trades.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveTrade(row);
                tradesCount_.fetch_add(1, std::memory_order_acq_rel);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("trades", capturedTrade.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        } else if (event.stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
                bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(bookTickerCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(event.bookTicker, meta);
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
                const auto fileStatus = jsonSink_.appendBookTickerLine(row, jsonLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("bookticker");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "bookticker: failed to write bookticker.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveBookTicker(row);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("bookticker", capturedBookTicker.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        } else if (event.stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                if (!event.depth || !event.depthSides) continue;
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedDepth = cxet_bridge::CxetCaptureBridge::captureOrderBook(*event.depth, *event.depthSides, meta);
                auto row = makeDepthRow(capturedDepth);
                if (textEqualsAscii(config.exchange, "bitget")) {
                    normalizeFixedDepthSnapshotDelta(row, bitgetPreviousOrderbookLevels);
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto tapeLine = renderDepthTapeJsonLine(row);
                const auto sidecarLine = renderDepthRleSidecarJsonLine(row);
                const auto localBusLine = renderDepthJsonLine(row);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("orderbook.delta", std::string_view{meta.symbol.data}, localBusLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto fileStatus = jsonSink_.appendDepthTapeSidecarLines(row, tapeLine, sidecarLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("depth");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "depth: failed to write depth_tape/depth_sidecar";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveDepth(row);
                depthCount_.fetch_add(1, std::memory_order_acq_rel);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("depth", capturedDepth.tsNs,
                                            static_cast<std::uint64_t>(tapeLine.size() + sidecarLine.size() + 2u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        }
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;
    }

    if (tradesWarmupThread.joinable()) {
        tradesWarmupThread.join();
        (void)flushTradesWarmupIfReady();
    }

    nextManifestFlushNs = 0;
    (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
    traderMarket.closeAll();
    tradesRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    marketDataRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (marketDataThread_.joinable() && !marketDataRunning_.load(std::memory_order_acquire)) marketDataThread_.join();
    if (referenceDataThread_.joinable() && !referenceDataRunning_.load(std::memory_order_acquire)) referenceDataThread_.join();
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) liquidationsThread_.join();
}
}  // namespace hftrec::capture


