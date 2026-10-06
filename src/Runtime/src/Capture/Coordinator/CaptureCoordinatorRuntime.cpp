#include "CaptureCoordinator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "cxet/Canon/PositionAndExchange.hpp"
#include "cxet/Canon/Subtypes.hpp"
#include "CaptureCoordinatorInternal.hpp"
#include "CaptureCoordinatorRuntimeHelpers.hpp"
#include "../Serialization/JsonSerializers.hpp"
#include "../Session/Bridge/CxetCaptureBridge.hpp"
#include "cxet/Runtime/Reference/ReferenceVenueConfig.hpp"
#include "cxet/Api/History/Candles/CandleHistoryLoader.hpp"
#include "cxet/Api/History/Trades/TradeHistoryLoader.hpp"
#include "cxet/Primitives/Composite/Trade.hpp"
#include "cxet/Primitives/Composite/TieredCandleHistory.hpp"
#include "cxet/Primitives/Composite/StreamMeta.hpp"

#include "cxet/Os/TimeDelta.hpp"
#include "cxet/Primitives/Composite/OrderBookSnapshot.hpp"

namespace hftrec::capture {

namespace runtime {

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

std::int64_t candleDurationNs(std::string_view timeframe,
                              std::int64_t tier) noexcept {
    if (timeframe == "1m") return 60LL * 1'000'000'000LL;
    if (timeframe == "10m") return 10LL * 60LL * 1'000'000'000LL;
    if (timeframe == "15m") return 15LL * 60LL * 1'000'000'000LL;
    if (timeframe == "1d") return 24LL * 60LL * 60LL * 1'000'000'000LL;
    if (tier == 1) return 60LL * 1'000'000'000LL;
    if (tier == 2) return 15LL * 60LL * 1'000'000'000LL;
    if (tier == 3) return 24LL * 60LL * 60LL * 1'000'000'000LL;
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



replay::TradeRow makeHistoricalTradeRow(const cxet::composite::Trade& trade,
                                        std::string_view exchange,
                                        std::string_view market,
                                        std::string_view identitySymbol,
                                        const EventSequenceIds& sequenceIds) {
    replay::TradeRow row{};
    row.tradeId = trade.eventId.raw;
    row.symbol = std::string{identitySymbol};
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(trade.ts.raw);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = static_cast<std::int64_t>(trade.price().raw);
    row.qtyE8 = static_cast<std::int64_t>(trade.qty().raw);
    row.side = trade.initiatorSide() == cxet::composite::TradeInitiatorSide::Buyer ? 1
        : trade.initiatorSide() == cxet::composite::TradeInitiatorSide::Seller ? 0 : -1;
    row.isBuyerMaker = trade.initiatorSide() == cxet::composite::TradeInitiatorSide::Seller ? 1u : 0u;
    row.sideBuy = trade.initiatorSide() == cxet::composite::TradeInitiatorSide::Buyer ? 1u : 0u;
    row.arrival.flags = replay::EventArrivalHistoricalBackfill;
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

bool appendHistoricalTradesToWarmup(void* userData,
                                    const cxet::composite::Trade* rows,
                                    std::size_t rowCount) noexcept {
    auto* context = static_cast<TradesHistorySinkContext*>(userData);
    if (context == nullptr || context->state == nullptr || context->tradesCaptureSeq == nullptr || context->ingestSeq == nullptr) return false;
    std::lock_guard<std::mutex> lock(context->state->mutex);
    for (std::size_t i = 0u; i < rowCount; ++i) {
        if (rows[i].initiatorSide() == cxet::composite::TradeInitiatorSide::Unknown) {
            context->state->error = "canonical JSON trade corpus cannot represent an unknown initiator side";
            return false;
        }
    }
    context->state->historyRows.reserve(context->state->historyRows.size() + rowCount);
    for (std::size_t i = 0u; i < rowCount; ++i) {
        if (rows[i].ts.raw == 0u || rows[i].price().raw == 0u || rows[i].qty().raw == 0u) continue;
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
    row.eventId = bookTicker.eventId;
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
    row.eventId = depth.eventId;
    row.tsNs = static_cast<std::int64_t>(depth.tsNs);
    row.levels = makeOrderbookLevels(depth);
    return row;
}

bool containsLevel(const std::vector<replay::PricePair>& levels, const replay::PricePair& needle) noexcept {
    return std::find_if(levels.begin(), levels.end(), [&](const replay::PricePair& level) noexcept {
        return level.priceE8 == needle.priceE8 && level.side == needle.side;
    }) != levels.end();
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



cxet::runtime::reference::ReferenceVenueConfig makeReferenceVenueConfig(const CaptureConfig& config) {
    return internal::makeReferenceVenueConfig(config);
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
    if (!textEqualsAscii(config.exchange, "finam")) {
        errorText = "FINAM is the only retained candle-history route";
        return false;
    }

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
    const bool fetched = cxet::api::candles::loadOhlcvHistoryForVenue(
        makeReferenceVenueConfig(config),
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
            if (requestBuf.isWsBinary()) {
                errorText += " request_bytes=" + std::to_string(requestBuf.size());
            } else {
                errorText += " request=";
                errorText.append(requestBuf.data(), std::min<std::size_t>(requestBuf.size(), 240u));
            }
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

}  // namespace runtime

using namespace runtime;

namespace {

void configureCaptureWorkerThreadStack() noexcept {
#if defined(__linux__)
    static std::once_flag once;
    std::call_once(once, [] {
        constexpr std::size_t kCaptureWorkerStackBytes = 16u * 1024u * 1024u;
        pthread_attr_t attr{};
        if (pthread_getattr_default_np(&attr) != 0) return;

        std::size_t stackSize = 0u;
        const bool shouldGrow =
            pthread_attr_getstacksize(&attr, &stackSize) == 0 &&
            stackSize < kCaptureWorkerStackBytes;
        if (shouldGrow && pthread_attr_setstacksize(&attr, kCaptureWorkerStackBytes) == 0) {
            (void)pthread_setattr_default_np(&attr);
        }
        (void)pthread_attr_destroy(&attr);
    });
#endif
}

}  // namespace


Status CaptureCoordinator::captureCandlesOnce(const CaptureConfig& config) noexcept {
    if (!textEqualsAscii(config.exchange, "finam")) {
        lastError_ = "FINAM is the only retained candle-history route";
        return Status::InvalidArgument;
    }
    if (internal::primaryRouteSymbolText(config).empty() || sessionDir_.empty()) return Status::InvalidArgument;
    if (candlesCount_.load(std::memory_order_acquire) != 0u) return Status::Ok;

    Symbol symbol{};
    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
    if (symbol.data[0] == '\0') return Status::InvalidArgument;

    cxet::composite::TieredCandleHistory history{};
    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    const bool fetched = cxet::api::candles::loadTieredCandleHistoryForVenue(
        makeReferenceVenueConfig(config),
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
            row.exchange = config.exchange;
            row.market = config.market;
            row.symbol = std::string{internal::primaryIdentitySymbolText(config)};
            row.timeframe = tier == 1 ? "1m" : (tier == 2 ? "15m" : "1d");
            row.durationNs = candleDurationNs(row.timeframe, tier);
            row.tsNs = static_cast<std::int64_t>(candle.ts.raw);
            row.highE8 = static_cast<std::int64_t>(candle.high.raw);
            row.lowE8 = static_cast<std::int64_t>(candle.low.raw);
            row.quoteAmountE8 = static_cast<std::int64_t>(candle.quoteAmount.raw);
            row.arrival.flags = replay::EventArrivalHistoricalBackfill;
            if (row.tsNs <= 0 || row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) continue;
            const auto line = renderCandleJsonLine(row);
            const auto writeStatus = candlesWriter_.writeLine(line);
            if (!isOk(writeStatus)) return writeStatus;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                noteArrival_(row.arrival, row.tsNs);
            }
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
    if (!textEqualsAscii(config.exchange, "finam")) {
        lastError_ = "FINAM is the only retained candle-history route";
        return Status::InvalidArgument;
    }
    internal::ensureCxetInitialized();
    if (const auto envStatus = internal::loadCaptureEnv(config, lastError_); !isOk(envStatus)) {
        return envStatus;
    }
    if (const auto validateStatus = internal::validateSupportedConfig(config, lastError_); !isOk(validateStatus)) {
        return validateStatus;
    }
    if (const auto authStatus = internal::refreshFinamAuthForConfig(
            config,
            internal::finamConfigNeedsAccountId(config),
            lastError_); !isOk(authStatus)) {
        return authStatus;
    }

    std::vector<cxet::composite::Ohlcv> rows;
    std::size_t rowCount = 0u;
    std::string tfText;
    std::string errorText;
    if (!fetchDetailedCandlesRows(config, rows, rowCount, tfText, errorText)) {
        lastError_ = errorText;
        return detailedCandlesFetchStatus(errorText);
    }
    if (const auto authPersistStatus = internal::persistFinamAuthForConfig(config, lastError_);
        !isOk(authPersistStatus)) {
        return authPersistStatus;
    }
    if (lastError_.starts_with("candles2:")) lastError_.clear();
    return Status::Ok;
}

Status CaptureCoordinator::captureDetailedCandlesOnce(const CaptureConfig& config) noexcept {
    if (!textEqualsAscii(config.exchange, "finam")) {
        lastError_ = "FINAM is the only retained candle-history route";
        return Status::InvalidArgument;
    }
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;
    if (detailedCandlesNeedInstrumentMetadata(config) && !instrumentMetadataReady_) {
        const auto metadataStatus = refreshInstrumentMetadataFromExchangeInfo();
        if (!isOk(metadataStatus)) return metadataStatus;
    }
    if (detailedCandlesNeedInstrumentMetadata(config) && !instrumentMetadataReady_) {
        const std::string metadataDetail = lastError_;
        lastError_ = "candles2: FINAM futures instrument metadata is required for price-basis logic; "
                     "refresh Finam auth and retry so /v1/assets/<symbol> can provide price_basis_qty_e8";
        if (!metadataDetail.empty()) {
            lastError_ += " metadata=";
            lastError_ += metadataDetail;
        }
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
    if (const auto authPersistStatus = internal::persistFinamAuthForConfig(config, lastError_);
        !isOk(authPersistStatus)) {
        return authPersistStatus;
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
        row.durationNs = candleDurationNs(tfText, candleTier);
        row.tsNs = static_cast<std::int64_t>(candle.ts.raw);
        row.openE8 = static_cast<std::int64_t>(candle.open.raw);
        row.highE8 = static_cast<std::int64_t>(candle.high.raw);
        row.lowE8 = static_cast<std::int64_t>(candle.low.raw);
        row.closeE8 = static_cast<std::int64_t>(candle.close.raw);
        row.volumeE8 = static_cast<std::int64_t>(candle.amount.raw);
        row.quoteAmountE8 = static_cast<std::int64_t>(candle.quoteAmount.raw);
        row.hasOhlc = true;
        row.arrival.flags = replay::EventArrivalHistoricalBackfill;
        if (!validDetailedCandle(candle) || row.volumeE8 < 0 || row.quoteAmountE8 < 0) {
            continue;
        }
        const auto line = renderCandleJsonLine(row);
        const auto writeStatus = candles2Writer_.writeLine(line);
        if (!isOk(writeStatus)) {
            lastError_ = "candles2: failed to write candles2.jsonl";
            return writeStatus;
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            noteArrival_(row.arrival, row.tsNs);
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
            lite.exchange = row.exchange;
            lite.market = row.market;
            lite.symbol = row.symbol;
            lite.timeframe = row.timeframe;
            lite.durationNs = row.durationNs;
            lite.arrival = row.arrival;
            const auto legacyLine = renderCandleJsonLine(lite);
            const auto legacyWriteStatus = candlesWriter_.writeLine(legacyLine);
            if (!isOk(legacyWriteStatus)) {
                lastError_ = "candles2: failed to write compatibility candles.jsonl";
                return legacyWriteStatus;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                noteArrival_(lite.arrival, lite.tsNs);
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
    const bool fetched = cxet::api::trades::loadPublicTradeHistoryForVenue(
        makeReferenceVenueConfig(config),
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
        const auto jsonLine = renderTradeJsonLine(row);
        const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
        if (!isOk(fileStatus)) {
            lastError_ = "trades_history: failed to write trades.jsonl";
            return fileStatus;
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            noteArrival_(row.arrival, row.tsNs);
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
    if (const auto authPersistStatus = internal::persistFinamAuthForConfig(config, lastError_);
        !isOk(authPersistStatus)) {
        (void)writeManifestFile_();
        return authPersistStatus;
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

    const auto sessionStatus = ensureSession_(config, true);
    if (!isOk(sessionStatus)) return sessionStatus;

    configureCaptureWorkerThreadStack();

    CaptureConfig normalizedConfig = config;
    switch (stream) {
        case ManagedStreamKind::Trades: {
            if (!internal::validateRequestedAliases(normalizedConfig.tradesAliases, lastError_)) {
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
            if (!internal::validateRequestedAliases(normalizedConfig.bookTickerAliases, lastError_)) {
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
            if (!internal::validateRequestedAliases(normalizedConfig.orderbookAliases, lastError_)) {
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
        case ManagedStreamKind::Trades:desiredTrades_.store(false,std::memory_order_release);tradesRunning_.store(false,std::memory_order_release);break;
        case ManagedStreamKind::BookTicker:desiredBookTicker_.store(false,std::memory_order_release);bookTickerRunning_.store(false,std::memory_order_release);break;
        case ManagedStreamKind::Orderbook:desiredOrderbook_.store(false,std::memory_order_release);orderbookRunning_.store(false,std::memory_order_release);break;
    }
    if (!anyManagedMarketDataDesired_()) marketDataStop_.store(true,std::memory_order_release);
}
bool CaptureCoordinator::anyManagedMarketDataDesired_() const noexcept {
    return desiredTrades_.load(std::memory_order_acquire)||desiredBookTicker_.load(std::memory_order_acquire)||desiredOrderbook_.load(std::memory_order_acquire);
}
void CaptureCoordinator::joinManagedMarketDataIfIdle_() noexcept {
    if (!anyManagedMarketDataDesired_() && marketDataThread_.joinable()) marketDataThread_.join();
}
}  // namespace hftrec::capture
