#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "api/run/RunByConfig.hpp"
#include "api/run/RunByConfigFetch.hpp"
#include "api/candles/TieredCandleHistoryFetch.hpp"
#include "api/trades/HistoricalTradesFetch.hpp"
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
#include "primitives/buf/TimeframeBuf.hpp"
#include "primitives/composite/OrderBookDeltaRuntimeV1.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/TieredCandleHistory.hpp"
#include "primitives/composite/Ohlcv.hpp"
#include "primitives/composite/StreamSoABuffer.hpp"
#include "primitives/composite/StreamMeta.hpp"
#include "network/rest/FetchRestKeepAlive.hpp"
#include "network/rest/RestKeepAlivePolicy.hpp"

#include "metrics/MetricsControl.hpp"
#include "metrics/Probes.hpp"
#include "probes/TimeDelta.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"

namespace hftrec::capture {

namespace {

constexpr unsigned kRecorderTradesKeepaliveMs = 0u;
constexpr unsigned kRecorderQuietStreamKeepaliveMs = 5000u;
constexpr unsigned kRecorderReconnectRetryMs = 100u;
constexpr std::int64_t kTradesHistoryWarmupMaxSec = 86400;
constexpr std::size_t kTradesHistoryWarmupTargetRows = 0u;
constexpr std::uint32_t kTradesHistoryWarmupPageLimit = 1000u;

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
                                        const EventSequenceIds& sequenceIds) {
    replay::TradeRow row{};
    row.tradeId = static_cast<std::uint64_t>(trade.id.raw);
    row.firstTradeId = static_cast<std::uint64_t>(trade.firstTradeId.raw);
    row.lastTradeId = static_cast<std::uint64_t>(trade.lastTradeId.raw);
    row.symbol = trade.symbol.data;
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
        context->state->historyRows.push_back(makeHistoricalTradeRow(rows[i], context->exchange, context->market, ids));
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

bool readLiquidationSnapshot(const cxet::api::market::PublicMarketDataSnapshot& snapshot,
                             cxet::composite::LiquidationEvent& liquidation,
                             cxet::composite::StreamMeta& meta) noexcept {
    for (std::uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const std::uint64_t before = snapshot.version.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const auto stream = snapshot.lastStream;
        const auto status = snapshot.lastStatus;
        const auto hasLiquidation = snapshot.hasLiquidation;
        meta = snapshot.meta;
        liquidation = snapshot.liquidation;
        const std::uint64_t after = snapshot.version.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            return stream == cxet::api::market::PublicMarketDataStream::Liquidations
                && status == cxet::api::market::PublicMarketDataStatus::Parsed
                && hasLiquidation != 0u;
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
    if (textEqualsAscii(exchange, "bybit")) return canon::kExchangeIdBybit;
    if (textEqualsAscii(exchange, "kucoin")) return canon::kExchangeIdKucoin;
    if (textEqualsAscii(exchange, "gate")) return canon::kExchangeIdGate;
    if (textEqualsAscii(exchange, "bitget")) return canon::kExchangeIdBitget;
    if (textEqualsAscii(exchange, "aster")) return canon::kExchangeIdAster;
    if (textEqualsAscii(exchange, "okx")) return canon::kExchangeIdOkx;
    return canon::kExchangeIdUnknown;
}

canon::MarketType marketTypeFromConfig(std::string_view market) noexcept {
    if (textEqualsAscii(market, "spot")) return canon::kMarketTypeSpot;
    if (textEqualsAscii(market, "margin")) return canon::kMarketTypeMargin;
    if (textEqualsAscii(market, "inverse")) return canon::kMarketTypeInverse;
    if (textEqualsAscii(market, "swap")) return canon::kMarketTypeSwap;
    return canon::kMarketTypeFutures;
}

const char* marketDataStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept {
    switch (stream) {
        case cxet::api::market::PublicMarketDataStream::BookTicker: return "bookticker";
        case cxet::api::market::PublicMarketDataStream::Trades: return "trades";
        case cxet::api::market::PublicMarketDataStream::Orderbook: return "orderbook";
        case cxet::api::market::PublicMarketDataStream::Liquidations: return "liquidations";
    }
    return "unknown";
}

std::string routePrepareErrorText(const CaptureConfig& config,
                                  cxet::api::market::PublicMarketDataStream stream) {
    const std::string symbol = config.symbols.empty() ? std::string{} : config.symbols.front();
    return std::string{"unsupported recorder market-data route"}
        + " stream=" + marketDataStreamName(stream)
        + " exchange=" + config.exchange
        + " market=" + config.market
        + " symbol=" + symbol;
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
    const bool futures = textEqualsAscii(config.market, "futures") || textEqualsAscii(config.market, "futures_usd");
    const bool bitgetSbe = textEqualsAscii(config.exchange, "bitget") &&
        (futures || textEqualsAscii(config.market, "spot") || textEqualsAscii(config.market, "inverse") ||
         textEqualsAscii(config.market, "swap"));
    const bool gateSbe = textEqualsAscii(config.exchange, "gate") && futures;
    return bitgetSbe || gateSbe
        ? cxet::api::market::PublicMarketDataWirePreference::Sbe
        : cxet::api::market::PublicMarketDataWirePreference::Auto;
}
bool shouldFetchInitialOrderbookSnapshot(const CaptureConfig& config) noexcept {
    return !textEqualsAscii(config.exchange, "bitget");
}

constexpr std::uint64_t kRecorderCandlePageCount = 200u;

struct CandleFetchDiagnostics {
    std::string exchange;
    std::string symbol;
    std::string timeframe;
    std::string stage;
    std::string host;
    std::string path;
    int httpStatus{0};
    std::size_t responseBytes{0};
};

std::string candleFetchDiagnosticsText(const CandleFetchDiagnostics& diagnostics) {
    std::ostringstream out;
    out << "stage=" << (diagnostics.stage.empty() ? "unknown" : diagnostics.stage);
    if (!diagnostics.exchange.empty()) out << " exchange=" << diagnostics.exchange;
    if (!diagnostics.symbol.empty()) out << " symbol=" << diagnostics.symbol;
    if (!diagnostics.timeframe.empty()) out << " tf=" << diagnostics.timeframe;
    if (!diagnostics.host.empty()) out << " host=" << diagnostics.host;
    if (!diagnostics.path.empty()) out << " path=" << diagnostics.path;
    out << " http=" << diagnostics.httpStatus;
    out << " bytes=" << diagnostics.responseBytes;
    return out.str();
}

const char* exchangeNameForDiagnostics(ExchangeId exchange) noexcept {
    if (exchange.raw == canon::kExchangeIdBinance.raw) return "binance";
    if (exchange.raw == canon::kExchangeIdBybit.raw) return "bybit";
    if (exchange.raw == canon::kExchangeIdKucoin.raw) return "kucoin";
    if (exchange.raw == canon::kExchangeIdGate.raw) return "gate";
    if (exchange.raw == canon::kExchangeIdBitget.raw) return "bitget";
    return "unknown";
}

void setCandleTimeframe(TimeframeBuf& out, const char* value) noexcept {
    out.copyFrom(value);
}

bool candleLessByTs(const cxet::composite::Ohlcv& lhs, const cxet::composite::Ohlcv& rhs) noexcept {
    return lhs.ts.raw < rhs.ts.raw;
}

std::uint64_t previousCandleNs(std::uint64_t ts) noexcept {
    return ts > 0u ? (ts - 1u) : 0u;
}

std::uint64_t currentUtcNs() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
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

std::size_t copyRecentCandles(const cxet::composite::Ohlcv* in,
                              std::size_t inCount,
                              std::uint64_t endNs,
                              cxet::composite::CandleLite* out) noexcept {
    std::array<cxet::composite::Ohlcv, cxet::composite::kTieredCandleHistoryCapacity> sorted{};
    std::size_t filtered = 0u;
    for (std::size_t i = 0u; i < inCount && filtered < sorted.size(); ++i) {
        if (in[i].ts.raw == 0u) continue;
        if (endNs != 0u && in[i].ts.raw > endNs) continue;
        sorted[filtered++] = in[i];
    }
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(filtered), candleLessByTs);

    std::size_t outCount = 0u;
    for (std::size_t i = 0u; i < filtered; ++i) {
        out[outCount].ts = sorted[i].ts;
        out[outCount].high = sorted[i].high;
        out[outCount].low = sorted[i].low;
        out[outCount].quoteAmount = sorted[i].quoteAmount;
        ++outCount;
    }
    return outCount;
}

bool runGetKlinesWithoutTradingSync(cxet::UnifiedRequestBuilder& builder,
                                    MessageBuffer& requestBuf,
                                    MessageBuffer& recvBuf,
                                    cxet::composite::Ohlcv* out,
                                    std::size_t outCapacity,
                                    std::size_t* outCount,
                                    CandleFetchDiagnostics* diagnostics) noexcept {
    if (!out || !outCount || outCapacity == 0u) return false;
    *outCount = 0u;
    const auto* config = cxet::api::findObjectConfig(static_cast<std::uint8_t>(builder.operation()),
                                                     builder.objectRaw(),
                                                     builder.exchange().raw,
                                                     builder.market().raw,
                                                     builder.typeRaw(),
                                                     builder.subtypeRaw());
    if (diagnostics) {
        diagnostics->exchange = exchangeNameForDiagnostics(builder.exchange());
        diagnostics->symbol = builder.symbolCount() > 0u ? builder.symbol(0).data : "";
        diagnostics->stage.clear();
        diagnostics->host.clear();
        diagnostics->path.clear();
        diagnostics->httpStatus = 0;
        diagnostics->responseBytes = 0u;
    }
    if (!config || builder.operation() != cxet::UnifiedRequestBuilder::Operation::Get || !config->parseKlinesFn) {
        if (diagnostics) diagnostics->stage = "route";
        return false;
    }
    if (!cxet::api::buildFromConfig(builder, *config, requestBuf)) {
        if (diagnostics) diagnostics->stage = "build";
        return false;
    }
    if (diagnostics) {
        diagnostics->host = config->rest.host ? config->rest.host : "";
        diagnostics->path.assign(requestBuf.data(), requestBuf.size());
    }
    cxet::primitives::network::RestKeepAlivePolicy policy{};
    policy.reuse = true;
    int httpStatus = 0;
    const bool fetched = (config->restHeaderName && config->restHeaderValue)
        ? cxet::network::rest::fetchRestKeepAlive(config->rest.host,
                                                  static_cast<std::uint16_t>(config->rest.port),
                                                  requestBuf,
                                                  recvBuf,
                                                  policy,
                                                  config->restHeaderName,
                                                  config->restHeaderValue,
                                                  &httpStatus)
        : cxet::network::rest::fetchRestKeepAlive(config->rest.host,
                                                  static_cast<std::uint16_t>(config->rest.port),
                                                  requestBuf,
                                                  recvBuf,
                                                  policy,
                                                  &httpStatus);
    if (diagnostics) {
        diagnostics->httpStatus = httpStatus;
        diagnostics->responseBytes = recvBuf.size();
    }
    if (!fetched) {
        if (diagnostics) diagnostics->stage = "fetch";
        return false;
    }

    cxet::composite::StreamSoABuffer stream;
    if (!stream.reserve(outCapacity)) return false;
    stream.meta.exchangeId.raw = config->exchangeRaw;
    stream.meta.objectType = cxet::composite::StreamObjectType::Ohlcv;
    stream.meta.market.raw = config->marketRaw;
    if (builder.symbolCount() > 0u) stream.meta.symbol = builder.symbol(0);

    const bool parsed = config->parseKlinesFn(recvBuf, stream, 0u, outCount, builder.requestedFields());
    if (!parsed) {
        if (diagnostics) diagnostics->stage = "parse";
        stream.freeAll();
        return false;
    }

    for (std::size_t i = 0u; i < *outCount && i < outCapacity; ++i) {
        out[i].exchangeId = stream.meta.exchangeId;
        out[i].symbol = stream.meta.symbol;
        out[i].open.raw = stream.col_open.data[i];
        out[i].high.raw = stream.col_high.data[i];
        out[i].low.raw = stream.col_low.data[i];
        out[i].close.raw = stream.col_close.data[i];
        out[i].amount.raw = stream.col_ohlcvAmount.data[i];
        out[i].quoteAmount.raw = stream.col_ohlcvQuoteAmount.data[i];
        out[i].ts.raw = stream.col_ohlcvTs.data[i];
        out[i].closeTs.raw = stream.col_closeTs.data[i];
        out[i].tradesCount.raw = stream.col_tradesCount.data[i];
        out[i].takerBuyBaseAmount.raw = stream.col_takerBuyBaseAmount.data[i];
        out[i].takerBuyQuoteAmount.raw = stream.col_takerBuyQuoteAmount.data[i];
    }

    stream.freeAll();
    if (*outCount == 0u) {
        if (diagnostics) diagnostics->stage = "empty";
        return false;
    }
    return true;
}

cxet::composite::CandleHistoryTierStatus fetchCandleTierWithoutTradingSync(ExchangeId exchange,
                                                                            canon::MarketType market,
                                                                            const Symbol& symbol,
                                                                            const char* timeframe,
                                                                            std::uint64_t endNs,
                                                                            cxet::composite::CandleLite* out,
                                                                            CountVal& outCount,
                                                                            MessageBuffer& requestBuf,
                                                                            MessageBuffer& recvBuf,
                                                                            std::uint8_t apiSlot,
                                                                            CandleFetchDiagnostics* diagnostics) noexcept {
    outCount.raw = 0u;
    std::array<cxet::composite::Ohlcv, cxet::composite::kTieredCandleHistoryCapacity> collected{};
    std::size_t collectedCount = 0u;
    std::uint64_t cursorEndNs = endNs;

    while (collectedCount < collected.size()) {
        bool pageOk = false;
        for (std::uint32_t attempt = 0u; attempt < 3u && !pageOk; ++attempt) {
            cxet::UnifiedRequestBuilder builder;
            builder.get();
            TimeframeBuf tf{};
            setCandleTimeframe(tf, timeframe);
            if (diagnostics) diagnostics->timeframe = timeframe ? timeframe : "";
            CountVal limit{};
            limit.raw = static_cast<std::uint32_t>(kRecorderCandlePageCount);
            builder.object(cxet::composite::out::GetObject::Klines)
                .exchange(exchange)
                .market(market)
                .symbol(symbol)
                .timeframe(tf)
                .limit(limit)
                .api(apiSlot);
            if (cursorEndNs != 0u) {
                TimeNs end{};
                end.raw = cursorEndNs;
                builder.endTime(end);
            }

            std::array<cxet::composite::Ohlcv, kRecorderCandlePageCount> raw{};
            std::size_t rawCount = 0u;
            if (!runGetKlinesWithoutTradingSync(builder, requestBuf, recvBuf, raw.data(), raw.size(), &rawCount, diagnostics)) continue;
            std::sort(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(rawCount), candleLessByTs);

            std::size_t eligibleBegin = 0u;
            while (eligibleBegin < rawCount && raw[eligibleBegin].ts.raw == 0u) ++eligibleBegin;
            std::size_t eligibleEnd = eligibleBegin;
            while (eligibleEnd < rawCount && (cursorEndNs == 0u || raw[eligibleEnd].ts.raw <= cursorEndNs)) ++eligibleEnd;

            const std::size_t remaining = collected.size() - collectedCount;
            const std::size_t eligibleCount = eligibleEnd - eligibleBegin;
            const std::size_t copyBegin = eligibleCount > remaining ? (eligibleEnd - remaining) : eligibleBegin;

            std::size_t pageCopied = 0u;
            std::uint64_t pageOldest = 0u;
            for (std::size_t i = copyBegin; i < eligibleEnd && collectedCount < collected.size(); ++i) {
                if (raw[i].ts.raw == 0u) continue;
                if (pageOldest == 0u || raw[i].ts.raw < pageOldest) pageOldest = raw[i].ts.raw;
                collected[collectedCount++] = raw[i];
                ++pageCopied;
            }
            if (pageCopied == 0u || pageOldest == 0u) continue;
            cursorEndNs = previousCandleNs(pageOldest);
            pageOk = true;
        }
        if (!pageOk) break;
    }

    const std::size_t copied = copyRecentCandles(collected.data(), collectedCount, endNs, out);
    if (copied == 0u) return cxet::composite::CandleHistoryTierStatus::FetchFailed;
    outCount.raw = static_cast<std::uint32_t>(copied);
    return cxet::composite::CandleHistoryTierStatus::Ok;
}

std::uint64_t oldestCandleTs(const cxet::composite::CandleLite* candles, CountVal count) noexcept {
    return count.raw == 0u ? 0u : candles[0].ts.raw;
}

bool fetchTieredCandlesWithoutTradingSync(ExchangeId exchange,
                                          canon::MarketType market,
                                          const Symbol& symbol,
                                          cxet::composite::TieredCandleHistory& out,
                                          MessageBuffer& requestBuf,
                                          MessageBuffer& recvBuf,
                                          std::uint8_t apiSlot,
                                          CandleFetchDiagnostics* diagnostics) noexcept {
    out = cxet::composite::TieredCandleHistory{};
    out.m1Status = fetchCandleTierWithoutTradingSync(exchange, market, symbol, "1m", currentUtcNs(), out.m1, out.m1Count, requestBuf, recvBuf, apiSlot, diagnostics);
    if (out.m1Status != cxet::composite::CandleHistoryTierStatus::Ok) return false;

    const std::uint64_t m15End = previousCandleNs(oldestCandleTs(out.m1, out.m1Count));
    out.m15Status = fetchCandleTierWithoutTradingSync(exchange, market, symbol, "15m", m15End, out.m15, out.m15Count, requestBuf, recvBuf, apiSlot, diagnostics);
    if (out.m15Status == cxet::composite::CandleHistoryTierStatus::Ok) {
        const std::uint64_t d1End = previousCandleNs(oldestCandleTs(out.m15, out.m15Count));
        out.d1Status = fetchCandleTierWithoutTradingSync(exchange, market, symbol, "1d", d1End, out.d1, out.d1Count, requestBuf, recvBuf, apiSlot, diagnostics);
    }
    return out.m1Status == cxet::composite::CandleHistoryTierStatus::Ok ||
           out.m15Status == cxet::composite::CandleHistoryTierStatus::Ok ||
           out.d1Status == cxet::composite::CandleHistoryTierStatus::Ok;
}

std::string candleHistoryStatusText(const cxet::composite::TieredCandleHistory& history) {
    return "m1=" + std::string(candleTierStatusName(history.m1Status)) + " count=" + std::to_string(history.m1Count.raw) +
           ", m15=" + std::string(candleTierStatusName(history.m15Status)) + " count=" + std::to_string(history.m15Count.raw) +
           ", d1=" + std::string(candleTierStatusName(history.d1Status)) + " count=" + std::to_string(history.d1Count.raw);
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
    return false;
}

}  // namespace


Status CaptureCoordinator::captureCandlesOnce(const CaptureConfig& config) noexcept {
    if (config.symbols.empty() || sessionDir_.empty()) return Status::InvalidArgument;
    if (candlesCount_.load(std::memory_order_acquire) != 0u) return Status::Ok;

    Symbol symbol{};
    symbol.copyFrom(config.symbols.front().c_str());
    if (symbol.data[0] == '\0') return Status::InvalidArgument;

    cxet::composite::TieredCandleHistory history{};
    CandleFetchDiagnostics candleDiagnostics{};
    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    const bool fetched = fetchTieredCandlesWithoutTradingSync(
        exchangeIdFromConfig(config.exchange),
        marketTypeFromConfig(config.market),
        symbol,
        history,
        requestBuf,
        recvBuf,
        1u,
        &candleDiagnostics);
    if (!fetched) {
        lastError_ = "candles: REST history fetch failed (" + candleHistoryStatusText(history) + "; "
                   + candleFetchDiagnosticsText(candleDiagnostics) + ")";
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
    if (textEqualsAscii(config.market, "spot") || textEqualsAscii(config.market, "margin")) {
        lastError_ = "liquidations are unsupported for spot or margin capture";
        return Status::InvalidArgument;
    }
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
    if (!cxet::api::market::rebuildPublicMarketDataRoute(route)) return DirectRouteConnectStatus::BadConfig;
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
        lastError_ = routePrepareErrorText(config, cxet::api::market::PublicMarketDataStream::BookTicker);
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

void CaptureCoordinator::liquidationsLoop_(CaptureConfig config) noexcept {
    constexpr canon::FieldId kFields[] = {
        canon::kFieldIdSymbol,
        canon::kFieldIdPrice,
        canon::kFieldIdAmount,
        canon::kFieldIdSide,
        canon::kFieldIdTimestamp,
        canon::kFieldIdAvgPrice,
        canon::kFieldIdFilled,
        canon::kFieldIdOrderType,
        canon::kFieldIdTimeInForce,
        canon::kFieldIdStatus,
    };

    auto builder = internal::makeLiquidationBuilder(config);
    std::string fieldError;
    if (!internal::applyRequestedAliases(config.liquidationAliases, builder, fieldError)) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = fieldError;
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }

    RecorderMarketDataChannel channel{};
    if (!prepareRecorderMarketDataChannel(channel,
                                          cxet::api::market::PublicMarketDataStream::Liquidations,
                                          builder,
                                          wirePreferenceForConfig(config),
                                          Span<const canon::FieldId>(kFields, sizeof(kFields) / sizeof(kFields[0])),
                                          &liquidationsStop_,
                                          kRecorderQuietStreamKeepaliveMs,
                                          cxet::metrics::latencyEnabled())) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = routePrepareErrorText(config, cxet::api::market::PublicMarketDataStream::Liquidations);
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }

    auto connectRoute = [&]() noexcept -> bool {
        while (!liquidationsStop_.load(std::memory_order_acquire)) {
            if (connectRecorderMarketDataRoute(channel.route) == cxet::api::market::PublicMarketDataStatus::Ok) return true;
            const auto* route = &channel.route;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = std::string{"liquidations route connect failed"}
                    + " host=" + (route && route->route.endpointHost ? route->route.endpointHost : "<null>")
                    + " path=" + std::string(route && route->route.connectPath ? route->route.connectPath : "<null>",
                                              route && route->route.connectPath ? route->route.connectPathLen : 6u)
                    + " payload=" + std::string(route && route->route.payload ? route->route.payload : "<null>",
                                                 route && route->route.payload ? std::min<std::size_t>(route->route.payloadLen, 160u) : 6u)
                    + " ws_error=" + std::to_string(route ? route->client.lastConnectError() : 0);
            }
            if (!sleepCaptureStopAware(&liquidationsStop_, kRecorderReconnectRetryMs)) break;
        }
        return false;
    };

    if (!connectRoute()) {
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }

    while (!liquidationsStop_.load(std::memory_order_acquire)) {
        const auto status = cxet::api::market::pollPublicMarketDataRouteOnce(channel.route);
        if (status == cxet::api::market::PublicMarketDataStatus::Parsed) {
            cxet::composite::LiquidationEvent liquidation{};
            cxet::composite::StreamMeta meta{};
            if (!readLiquidationSnapshot(channel.snapshot, liquidation, meta)) continue;
            const auto sequenceIds = nextEventSequenceIds(liquidationsCaptureSeq_, ingestSeq_);
            const auto capturedLiquidation = cxet_bridge::CxetCaptureBridge::captureLiquidation(liquidation);
            const auto row = makeLiquidationRow(capturedLiquidation, config.exchange, config.market, sequenceIds);
            const auto jsonLine = renderLiquidationJsonLine(row, config.liquidationAliases);
            local_exchange::globalLocalMarketDataBus().publish("liquidations", row.symbol, jsonLine);
            const auto liveStatus = liveStore_.appendLiquidation(row);
            const auto fileStatus = isOk(liveStatus) ? jsonSink_.appendLiquidationLine(row, jsonLine) : liveStatus;
            if (!isOk(fileStatus)) {
                metrics::recordCaptureWriteError("liquidations");
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "liquidations: failed to write liquidations.jsonl";
                liquidationsStop_.store(true, std::memory_order_release);
                break;
            }
            liquidationsCount_.fetch_add(1, std::memory_order_acq_rel);
            metrics::recordCaptureEvent("liquidations",
                                        capturedLiquidation.tsNs,
                                        static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                        static_cast<std::uint64_t>(internal::nowNs()));
            continue;
        }
        if (status == cxet::api::market::PublicMarketDataStatus::Disconnected
            || status == cxet::api::market::PublicMarketDataStatus::ConnectFailed) {
            cxet::api::market::closePublicMarketDataRoute(channel.route);
            (void)connectRoute();
        }
    }
    cxet::api::market::closePublicMarketDataRoute(channel.route);
    liquidationsRunning_.store(false, std::memory_order_release);
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
    TradesHistoryWarmupState tradesWarmup{};
    std::thread tradesWarmupThread{};
    std::vector<replay::TradeRow> bufferedLiveTradeRows{};
    bool tradesWarmupFlushed = false;
    std::size_t tradesWarmupDisplayedRows = 0u;

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
                lastError_ = routePrepareErrorText(config, stream);
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

    auto appendTradeRowToLiveOnly = [&](const replay::TradeRow& row, const std::string& jsonLine) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        local_exchange::globalLocalMarketDataBus().publish(kTradesText, row.symbol, jsonLine);
        const auto liveStatus = liveStore_.appendTrade(row);
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
        const auto liveStatus = liveStore_.appendTrade(row);
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
            cxet::api::trades::HistoricalTradesRequest request{};
            request.exchange = exchangeIdFromConfig(config.exchange);
            request.market = marketTypeFromConfig(config.market);
            request.startTime.raw = static_cast<std::uint64_t>(startNs > 0 ? startNs : 0);
            request.endTime.raw = static_cast<std::uint64_t>(endNs > 0 ? endNs : 0);
            request.pageLimit.raw = kTradesHistoryWarmupPageLimit;
            request.apiSlot = 1u;
            request.allowRecentFallback = true;
            if (!config.symbols.empty()) request.symbol.copyFrom(config.symbols.front().c_str());

            TradesHistorySinkContext sinkContext{};
            sinkContext.state = &tradesWarmup;
            sinkContext.tradesCaptureSeq = &tradesCaptureSeq_;
            sinkContext.ingestSeq = &ingestSeq_;
            sinkContext.exchange = config.exchange;
            sinkContext.market = config.market;
            sinkContext.maxRows = kTradesHistoryWarmupTargetRows;

            MessageBuffer requestBuf{};
            MessageBuffer recvBuf{};
            cxet::api::trades::HistoricalTradesResult result{};
            bool ok = cxet::api::trades::fetchHistoricalTrades(request,
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

    while (!marketDataStop_.load(std::memory_order_acquire)) {
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
            startTradesWarmupIfNeeded();
        }
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;

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
                if (tradesWarmup.started.load(std::memory_order_acquire)
                    && !tradesWarmupFlushed) {
                    bufferedLiveTradeRows.push_back(row);
                    if (!appendTradeRowToLiveOnly(row, jsonLine)) break;
                    continue;
                }
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
                tradesCount_.fetch_add(1, std::memory_order_acq_rel);
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
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;
        if (eventCount == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (tradesWarmupThread.joinable()) {
        tradesWarmupThread.join();
        (void)flushTradesWarmupIfReady();
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


