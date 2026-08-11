#include "core/capture/JsonSerializers.hpp"

#include <charconv>
#include <system_error>

#include "core/common/JsonString.hpp"
#include "core/replay/EventRows.hpp"
#include "primitives/composite/OrderBookTapeRuntimeV1.hpp"

namespace hftrec::capture {

namespace {

template <typename Int>
void appendInt(std::string& out, Int value) {
    char buf[32];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc{}) out.append(buf, ptr);
}

void appendString(std::string& out, std::string_view value) {
    out.push_back(static_cast<char>(34));
    json::appendEscaped(out, value);
    out.push_back(static_cast<char>(34));
}

void appendArrival(std::string& out, const replay::EventArrival& arrival) {
    appendInt(out, arrival.receiveRealtimeNs); out.push_back(',');
    appendInt(out, arrival.receiveMonotonicNs); out.push_back(',');
    appendInt(out, arrival.producerEpoch); out.push_back(',');
    appendInt(out, arrival.sourceGeneration); out.push_back(',');
    appendInt(out, arrival.sessionEpoch); out.push_back(',');
    appendInt(out, arrival.frameSequence); out.push_back(',');
    appendInt(out, arrival.shardSequence); out.push_back(',');
    appendInt(out, arrival.sourceId); out.push_back(',');
    appendInt(out, arrival.shardId); out.push_back(',');
    appendInt(out, arrival.eventOrdinal); out.push_back(',');
    appendInt(out, arrival.flags);
}

std::int64_t candleTierFromTimeframe(std::string_view timeframe) noexcept {
    if (timeframe == "1m") return 1;
    if (timeframe == "10m" || timeframe == "15m") return 2;
    if (timeframe == "1d") return 3;
    return 0;
}

void appendFlatOrderbook(std::string& out, const std::vector<replay::PricePair>& levels, std::int64_t tsNs) {
    out.push_back('[');
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out.push_back(',');
        out.push_back('[');
        appendInt(out, levels[i].priceE8);
        out.push_back(',');
        appendInt(out, levels[i].qtyE8);
        out.push_back(',');
        appendInt(out, levels[i].side);
        out.push_back(']');
    }
    if (!levels.empty()) out.push_back(',');
    appendInt(out, tsNs);
    out.push_back(']');
}

std::uint64_t taggedTapeTimestamp(std::int64_t tsNs) noexcept {
    TimeNs ts{};
    ts.raw = static_cast<std::uint64_t>(tsNs);
    return cxet::composite::makeOrderBookTapeTimestampWord(ts);
}

}  // namespace

std::string renderTradeJsonLine(const replay::TradeRow& trade) {
    std::string out;
    out.reserve(320);
    out.push_back('[');
    appendInt(out, trade.priceE8); out.push_back(',');
    appendInt(out, trade.qtyE8); out.push_back(',');
    appendInt(out, trade.side); out.push_back(',');
    appendInt(out, trade.tsNs); out.push_back(',');
    appendInt(out, trade.tradeId); out.push_back(',');
    appendInt(out, trade.firstTradeId); out.push_back(',');
    appendInt(out, trade.lastTradeId); out.push_back(',');
    appendInt(out, trade.quoteQtyE8); out.push_back(',');
    appendInt(out, static_cast<unsigned>(trade.isBuyerMaker)); out.push_back(',');
    appendString(out, trade.symbol); out.push_back(',');
    appendString(out, trade.exchange); out.push_back(',');
    appendString(out, trade.market); out.push_back(',');
    appendInt(out, trade.captureSeq); out.push_back(',');
    appendInt(out, trade.ingestSeq); out.push_back(',');
    appendArrival(out, trade.arrival);
    out.push_back(']');
    return out;
}

std::string renderTradeJsonLine(const replay::TradeRow& trade,
                                const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderTradeJsonLine(trade);
}

std::string renderLiquidationJsonLine(const replay::LiquidationRow& liquidation) {
    std::string out;
    out.reserve(384);
    out.push_back('[');
    appendInt(out, liquidation.priceE8); out.push_back(',');
    appendInt(out, liquidation.qtyE8); out.push_back(',');
    appendInt(out, liquidation.side); out.push_back(',');
    appendInt(out, liquidation.tsNs); out.push_back(',');
    appendInt(out, liquidation.avgPriceE8); out.push_back(',');
    appendInt(out, liquidation.filledQtyE8); out.push_back(',');
    appendString(out, liquidation.symbol); out.push_back(',');
    appendString(out, liquidation.exchange); out.push_back(',');
    appendString(out, liquidation.market); out.push_back(',');
    appendInt(out, liquidation.orderType); out.push_back(',');
    appendInt(out, liquidation.timeInForce); out.push_back(',');
    appendInt(out, liquidation.status); out.push_back(',');
    appendInt(out, liquidation.sourceMode); out.push_back(',');
    appendInt(out, liquidation.captureSeq); out.push_back(',');
    appendInt(out, liquidation.ingestSeq); out.push_back(',');
    appendArrival(out, liquidation.arrival);
    out.push_back(']');
    return out;
}

std::string renderLiquidationJsonLine(const replay::LiquidationRow& liquidation,
                                      const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderLiquidationJsonLine(liquidation);
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker) {
    std::string out;
    out.reserve(320);
    out.push_back('[');
    appendInt(out, bookTicker.eventId); out.push_back(',');
    appendInt(out, bookTicker.bidPriceE8); out.push_back(',');
    appendInt(out, bookTicker.bidQtyE8); out.push_back(',');
    appendInt(out, bookTicker.askPriceE8); out.push_back(',');
    appendInt(out, bookTicker.askQtyE8); out.push_back(',');
    appendInt(out, bookTicker.tsNs); out.push_back(',');
    appendString(out, bookTicker.symbol); out.push_back(',');
    appendString(out, bookTicker.exchange); out.push_back(',');
    appendString(out, bookTicker.market); out.push_back(',');
    appendInt(out, bookTicker.captureSeq); out.push_back(',');
    appendInt(out, bookTicker.ingestSeq); out.push_back(',');
    appendArrival(out, bookTicker.arrival);
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker,
                                     const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderBookTickerJsonLine(bookTicker);
}

std::string renderCandleJsonLine(const replay::CandleRow& candle) {
    std::string out;
    out.reserve(384);
    out.push_back('[');
    const std::int64_t tier = candle.tier > 0 ? candle.tier : candleTierFromTimeframe(candle.timeframe);
    appendInt(out, tier); out.push_back(',');
    appendInt(out, candle.tsNs); out.push_back(',');
    appendInt(out, candle.openE8); out.push_back(',');
    appendInt(out, candle.highE8); out.push_back(',');
    appendInt(out, candle.lowE8); out.push_back(',');
    appendInt(out, candle.closeE8); out.push_back(',');
    appendInt(out, candle.volumeE8); out.push_back(',');
    appendInt(out, candle.quoteAmountE8); out.push_back(',');
    appendInt(out, candle.hasOhlc ? 1u : 0u); out.push_back(',');
    appendString(out, candle.exchange); out.push_back(',');
    appendString(out, candle.market); out.push_back(',');
    appendString(out, candle.symbol); out.push_back(',');
    appendString(out, candle.timeframe); out.push_back(',');
    appendInt(out, candle.durationNs); out.push_back(',');
    appendInt(out, candle.captureSeq); out.push_back(',');
    appendInt(out, candle.ingestSeq); out.push_back(',');
    appendArrival(out, candle.arrival);
    out.push_back(']');
    return out;
}

std::string renderMarkPriceJsonLine(const replay::MarkPriceRow& row) {
    std::string out;
    out.reserve(224);
    out.push_back('[');
    appendInt(out, row.tsNs); out.push_back(',');
    appendInt(out, row.markPriceE8); out.push_back(',');
    appendInt(out, row.captureSeq); out.push_back(',');
    appendInt(out, row.ingestSeq); out.push_back(',');
    appendArrival(out, row.arrival);
    out.push_back(']');
    return out;
}

std::string renderIndexPriceJsonLine(const replay::IndexPriceRow& row) {
    std::string out;
    out.reserve(224);
    out.push_back('[');
    appendInt(out, row.tsNs); out.push_back(',');
    appendInt(out, row.indexPriceE8); out.push_back(',');
    appendInt(out, row.captureSeq); out.push_back(',');
    appendInt(out, row.ingestSeq); out.push_back(',');
    appendArrival(out, row.arrival);
    out.push_back(']');
    return out;
}

std::string renderFundingJsonLine(const replay::FundingRow& row) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    appendInt(out, row.tsNs); out.push_back(',');
    appendInt(out, row.fundingRateE8); out.push_back(',');
    appendInt(out, row.fundingTsNs); out.push_back(',');
    appendInt(out, row.nextFundingTsNs); out.push_back(',');
    appendInt(out, row.captureSeq); out.push_back(',');
    appendInt(out, row.ingestSeq); out.push_back(',');
    appendArrival(out, row.arrival);
    out.push_back(']');
    return out;
}

std::string renderPriceLimitJsonLine(const replay::PriceLimitRow& row) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    appendInt(out, row.tsNs); out.push_back(',');
    appendInt(out, row.buyLimitE8); out.push_back(',');
    appendInt(out, row.sellLimitE8); out.push_back(',');
    appendInt(out, static_cast<int>(row.enabled)); out.push_back(',');
    appendInt(out, row.captureSeq); out.push_back(',');
    appendInt(out, row.ingestSeq); out.push_back(',');
    appendArrival(out, row.arrival);
    out.push_back(']');
    return out;
}

std::string renderDepthTapeJsonLine(const replay::DepthRow& delta) {
    std::string out;
    out.reserve(224 + delta.levels.size() * 40);
    out.push_back('[');
    appendInt(out, delta.eventId); out.push_back(',');
    appendInt(out, taggedTapeTimestamp(delta.tsNs)); out.push_back(',');
    appendInt(out, delta.captureSeq); out.push_back(',');
    appendInt(out, delta.ingestSeq); out.push_back(',');
    appendArrival(out, delta.arrival);
    for (const auto& level : delta.levels) {
        out.push_back(',');
        appendInt(out, level.priceE8);
        out.push_back(',');
        appendInt(out, level.qtyE8);
    }
    out.push_back(']');
    return out;
}

std::string renderDepthRleSidecarJsonLine(const replay::DepthRow& delta) {
    std::string out;
    out.reserve(32 + delta.levels.size() * 6);
    out.push_back('[');
    appendInt(out, delta.eventId); out.push_back(',');
    appendInt(out, taggedTapeTimestamp(delta.tsNs));
    if (!delta.levels.empty()) {
        std::int64_t runSide = delta.levels.front().side;
        std::uint64_t runCount = 0;
        for (const auto& level : delta.levels) {
            if (level.side == runSide) {
                ++runCount;
                continue;
            }
            out.push_back(',');
            appendInt(out, runSide);
            out.push_back(',');
            appendInt(out, runCount);
            runSide = level.side;
            runCount = 1u;
        }
        out.push_back(',');
        appendInt(out, runSide);
        out.push_back(',');
        appendInt(out, runCount);
    }
    out.push_back(']');
    return out;
}

std::string renderSnapshotJson(const replay::SnapshotDocument& snapshot) {
    std::string out;
    out.reserve(64 + snapshot.levels.size() * 48);
    appendFlatOrderbook(out, snapshot.levels, snapshot.tsNs);
    out.push_back('\n');
    return out;
}

}  // namespace hftrec::capture
