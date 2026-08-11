#include "core/replay/JsonLineParser.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::replay {

namespace {
using JsonParser = hftrec::json::MiniJsonParser;

constexpr std::uint64_t orderBookTapeTimestampTag = 1ull << 63u;
constexpr std::uint64_t orderBookTapePayloadMask = orderBookTapeTimestampTag - 1u;

bool validateCandle(const CandleRow& row) noexcept {
    if (row.tsNs <= 0 || row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8 || row.quoteAmountE8 < 0) {
        return false;
    }
    if (!row.hasOhlc) return row.tier >= 1 && row.tier <= 3;
    const bool hasNumericTier = row.tier >= 1 && row.tier <= 3;
    return hasNumericTier
        && row.durationNs > 0
        && row.openE8 > 0
        && row.closeE8 > 0
        && row.volumeE8 >= 0;
}

bool parseCommaInt64(JsonParser& parser, std::int64_t& value) noexcept {
    return parser.parseComma() && parser.parseInt64(value);
}

bool parseCommaUInt64(JsonParser& parser, std::uint64_t& value) noexcept {
    return parser.parseComma() && parser.parseUInt64(value);
}

bool parseArrival(JsonParser& parser, EventArrival& out) noexcept {
    std::uint64_t sourceId = 0u;
    std::uint64_t shardId = 0u;
    std::uint64_t eventOrdinal = 0u;
    std::uint64_t flags = 0u;
    if (!parseCommaInt64(parser, out.receiveRealtimeNs) ||
        !parseCommaUInt64(parser, out.receiveMonotonicNs) ||
        !parseCommaUInt64(parser, out.producerEpoch) ||
        !parseCommaUInt64(parser, out.sourceGeneration) ||
        !parseCommaUInt64(parser, out.sessionEpoch) ||
        !parseCommaUInt64(parser, out.frameSequence) ||
        !parseCommaUInt64(parser, out.shardSequence) ||
        !parseCommaUInt64(parser, sourceId) ||
        !parseCommaUInt64(parser, shardId) ||
        !parseCommaUInt64(parser, eventOrdinal) ||
        !parseCommaUInt64(parser, flags) ||
        sourceId > std::numeric_limits<std::uint32_t>::max() ||
        shardId > std::numeric_limits<std::uint16_t>::max() ||
        eventOrdinal > std::numeric_limits<std::uint16_t>::max() ||
        flags > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out.sourceId = static_cast<std::uint32_t>(sourceId);
    out.shardId = static_cast<std::uint16_t>(shardId);
    out.eventOrdinal = static_cast<std::uint16_t>(eventOrdinal);
    out.flags = static_cast<std::uint32_t>(flags);
    if ((out.flags & ~kEventArrivalKnownFlags) != 0u) return false;
    const bool application =
        (out.flags & EventArrivalApplicationFrame) != 0u;
    const bool historical =
        (out.flags & EventArrivalHistoricalBackfill) != 0u;
    if (application && historical) return false;
    if (application) return hasCapturedApplicationArrival(out);
    if ((out.flags & ~(EventArrivalHistoricalBackfill |
                       EventArrivalExchangeTimestampMissing)) != 0u) {
        return false;
    }
    return out.receiveRealtimeNs == 0 && out.receiveMonotonicNs == 0u &&
        out.producerEpoch == 0u && out.sourceGeneration == 0u &&
        out.sessionEpoch == 0u && out.frameSequence == 0u &&
        out.shardSequence == 0u && out.sourceId == 0u && out.shardId == 0u &&
        out.eventOrdinal == 0u;
}

bool parsePricePair(JsonParser& parser, PricePair& out) noexcept {
    std::int64_t side = 0;
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.priceE8)) return false;
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8)) return false;
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(side)) return false;
    if (side != 0 && side != 1) return false;
    out.side = side;
    return parser.parseArrayEnd();
}

bool parseFlatOrderbook(JsonParser& parser, std::vector<PricePair>& levels, std::int64_t& tsNs) noexcept {
    levels.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return false;
    while (parser.peek('[')) {
        PricePair pair{};
        if (!parsePricePair(parser, pair)) return false;
        levels.push_back(pair);
        if (!parser.parseComma()) return false;
    }
    if (!parser.parseInt64(tsNs)) return false;
    return parser.parseArrayEnd() && parser.finish();
}

bool parseTapeNonNegativeI64(JsonParser& parser, std::int64_t& out) noexcept {
    std::uint64_t value = 0;
    if (!parser.parseUInt64(value)) return false;
    if ((value & orderBookTapeTimestampTag) != 0u) return false;
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
    out = static_cast<std::int64_t>(value);
    return true;
}

bool parseDepthTapeLine(std::string_view tapeLine, DepthRow& out) noexcept {
    out.eventId = 0u;
    out.tsNs = 0;
    out.captureSeq = 0;
    out.ingestSeq = 0;
    out.arrival = {};
    out.levels.clear();
    JsonParser parser{tapeLine};
    if (!parser.parseArrayStart()) return false;
    std::uint64_t word = 0;
    if (!parser.parseUInt64(word)) return false;
    out.eventId = word;
    if (out.eventId == 0u || !parser.parseComma() ||
        !parser.parseUInt64(word)) return false;
    if ((word & orderBookTapeTimestampTag) == 0u) return false;
    out.tsNs = static_cast<std::int64_t>(word & orderBookTapePayloadMask);
    if (!parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return false;
    while (!parser.peek(']')) {
        if (!parser.parseComma()) return false;
        std::int64_t price = 0;
        if (!parseTapeNonNegativeI64(parser, price)) return false;
        if (!parser.parseComma()) return false;
        std::int64_t qty = 0;
        if (!parseTapeNonNegativeI64(parser, qty)) return false;
        out.levels.push_back(PricePair{price, qty, 0});
    }
    return parser.parseArrayEnd() && parser.finish();
}

bool applyDepthRleSidecar(std::string_view sidecarLine, DepthRow& out) noexcept {
    JsonParser parser{sidecarLine};
    if (!parser.parseArrayStart()) return false;
    std::uint64_t word = 0;
    if (!parser.parseUInt64(word)) return false;
    if (word != out.eventId || !parser.parseComma() ||
        !parser.parseUInt64(word)) return false;
    if ((word & orderBookTapeTimestampTag) == 0u) return false;
    if (static_cast<std::int64_t>(word & orderBookTapePayloadMask) != out.tsNs) return false;

    std::size_t levelIndex = 0;
    while (!parser.peek(']')) {
        if (!parser.parseComma()) return false;
        std::int64_t sideValue = 0;
        if (!parser.parseInt64(sideValue) || (sideValue != 0 && sideValue != 1)) return false;
        if (!parser.parseComma()) return false;
        std::uint64_t runCount = 0;
        if (!parser.parseUInt64(runCount) || runCount == 0u) return false;
        if (runCount > static_cast<std::uint64_t>(out.levels.size() - levelIndex)) return false;
        for (std::uint64_t i = 0; i < runCount; ++i) {
            out.levels[levelIndex].side = sideValue;
            ++levelIndex;
        }
    }
    if (levelIndex != out.levels.size()) return false;
    return parser.parseArrayEnd() && parser.finish();
}

bool parseBoolByte(JsonParser& parser, std::uint8_t& out) noexcept {
    bool boolValue = false;
    if (parser.peek('t') || parser.peek('f')) {
        if (!parser.parseBool(boolValue)) return false;
        out = boolValue ? 1u : 0u;
        return true;
    }
    std::int64_t intValue = 0;
    if (!parser.parseInt64(intValue)) return false;
    if (intValue != 0 && intValue != 1) return false;
    out = static_cast<std::uint8_t>(intValue);
    return true;
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.side)) return Status::CorruptData;
    if (out.side != 0 && out.side != 1) return Status::CorruptData;
    out.sideBuy = static_cast<std::uint8_t>(out.side);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseUInt64(out.tradeId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseUInt64(out.firstTradeId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseUInt64(out.lastTradeId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.quoteQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parseBoolByte(parser, out.isBuyerMaker)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseTradeLine(std::string_view line,
                      TradeRow& out,
                      const std::vector<std::string>& aliases) noexcept {
    (void)aliases;
    return parseTradeLine(line, out);
}

Status parseLiquidationLine(std::string_view line, LiquidationRow& out) noexcept {
    out = LiquidationRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.side)) return Status::CorruptData;
    if (out.side != 0 && out.side != 1) return Status::CorruptData;
    out.sideBuy = static_cast<std::uint8_t>(out.side);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.avgPriceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.filledQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.orderType)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.timeInForce)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.status)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.sourceMode)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseLiquidationLine(std::string_view line,
                            LiquidationRow& out,
                            const std::vector<std::string>& aliases) noexcept {
    (void)aliases;
    return parseLiquidationLine(line, out);
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseUInt64(out.eventId) || out.eventId == 0u ||
        !parseCommaInt64(parser, out.bidPriceE8) ||
        !parseCommaInt64(parser, out.bidQtyE8) ||
        !parseCommaInt64(parser, out.askPriceE8) ||
        !parseCommaInt64(parser, out.askQtyE8) ||
        !parseCommaInt64(parser, out.tsNs) || !parser.parseComma() ||
        !parser.parseString(out.symbol) || !parser.parseComma() ||
        !parser.parseString(out.exchange) || !parser.parseComma() ||
        !parser.parseString(out.market) ||
        !parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival) || !parser.parseArrayEnd() ||
        !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line,
                           BookTickerRow& out,
                           const std::vector<std::string>& aliases) noexcept {
    (void)aliases;
    return parseBookTickerLine(line, out);
}

Status parseCandleLine(std::string_view line, CandleRow& out) noexcept {
    out = CandleRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    std::uint8_t hasOhlc = 0u;
    if (!parser.parseInt64(out.tier) ||
        !parseCommaInt64(parser, out.tsNs) ||
        !parseCommaInt64(parser, out.openE8) ||
        !parseCommaInt64(parser, out.highE8) ||
        !parseCommaInt64(parser, out.lowE8) ||
        !parseCommaInt64(parser, out.closeE8) ||
        !parseCommaInt64(parser, out.volumeE8) ||
        !parseCommaInt64(parser, out.quoteAmountE8) ||
        !parser.parseComma() || !parseBoolByte(parser, hasOhlc) ||
        !parser.parseComma() || !parser.parseString(out.exchange) ||
        !parser.parseComma() || !parser.parseString(out.market) ||
        !parser.parseComma() || !parser.parseString(out.symbol) ||
        !parser.parseComma() || !parser.parseString(out.timeframe) ||
        !parseCommaInt64(parser, out.durationNs) ||
        !parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return Status::CorruptData;
    out.hasOhlc = hasOhlc != 0u;
    if (!validateCandle(out) || !parser.parseArrayEnd() ||
        !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseMarkPriceLine(std::string_view line, MarkPriceRow& out) noexcept {
    out = MarkPriceRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.markPriceE8)) return Status::CorruptData;
    if (!parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (out.tsNs <= 0 || out.markPriceE8 <= 0) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseIndexPriceLine(std::string_view line, IndexPriceRow& out) noexcept {
    out = IndexPriceRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.indexPriceE8)) return Status::CorruptData;
    if (!parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (out.tsNs <= 0 || out.indexPriceE8 <= 0) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseFundingLine(std::string_view line, FundingRow& out) noexcept {
    out = FundingRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.fundingRateE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.fundingTsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.nextFundingTsNs)) return Status::CorruptData;
    if (!parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (out.tsNs <= 0) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parsePriceLimitLine(std::string_view line, PriceLimitRow& out) noexcept {
    out = PriceLimitRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.buyLimitE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.sellLimitE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parseBoolByte(parser, out.enabled)) return Status::CorruptData;
    if (!parseCommaInt64(parser, out.captureSeq) ||
        !parseCommaInt64(parser, out.ingestSeq) ||
        !parseArrival(parser, out.arrival)) return Status::CorruptData;
    if (out.tsNs <= 0 || out.buyLimitE8 < 0 || out.sellLimitE8 < 0) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthTapeSidecarLine(std::string_view tapeLine,
                                 std::string_view sidecarLine,
                                 DepthRow& out) noexcept {
    if (!parseDepthTapeLine(tapeLine, out)) return Status::CorruptData;
    return applyDepthRleSidecar(sidecarLine, out) ? Status::Ok : Status::CorruptData;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    return parseFlatOrderbook(parser, out.levels, out.tsNs) ? Status::Ok : Status::CorruptData;
}

}  // namespace hftrec::replay
