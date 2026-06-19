#include "core/replay/JsonLineParser.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::replay {

namespace {
using JsonParser = hftrec::json::MiniJsonParser;

constexpr char price[] = {'p','r','i','c','e','\0'};
constexpr char amount[] = {'a','m','o','u','n','t','\0'};
constexpr char side[] = {'s','i','d','e','\0'};
constexpr char timestamp[] = {'t','i','m','e','s','t','a','m','p','\0'};
constexpr char id[] = {'i','d','\0'};
constexpr char isBuyerMaker[] = {'i','s','B','u','y','e','r','M','a','k','e','r','\0'};
constexpr char firstTradeId[] = {'f','i','r','s','t','T','r','a','d','e','I','d','\0'};
constexpr char lastTradeId[] = {'l','a','s','t','T','r','a','d','e','I','d','\0'};
constexpr char quoteQty[] = {'q','u','o','t','e','Q','t','y','\0'};
constexpr char symbol[] = {'s','y','m','b','o','l','\0'};
constexpr char exchange[] = {'e','x','c','h','a','n','g','e','\0'};
constexpr char market[] = {'m','a','r','k','e','t','\0'};
constexpr char bidPrice[] = {'b','i','d','P','r','i','c','e','\0'};
constexpr char bidQty[] = {'b','i','d','Q','t','y','\0'};
constexpr char askPrice[] = {'a','s','k','P','r','i','c','e','\0'};
constexpr char askQty[] = {'a','s','k','Q','t','y','\0'};
constexpr char updateId[] = {'u','p','d','a','t','e','I','d','\0'};
constexpr char avgPrice[] = {'a','v','g','P','r','i','c','e','\0'};
constexpr char filledQty[] = {'f','i','l','l','e','d','Q','t','y','\0'};
constexpr char orderType[] = {'o','r','d','e','r','T','y','p','e','\0'};
constexpr char timeInForce[] = {'t','i','m','e','I','n','F','o','r','c','e','\0'};
constexpr char status[] = {'s','t','a','t','u','s','\0'};
constexpr char sourceMode[] = {'s','o','u','r','c','e','M','o','d','e','\0'};
constexpr char captureSeq[] = {'c','a','p','t','u','r','e','S','e','q','\0'};
constexpr char ingestSeq[] = {'i','n','g','e','s','t','S','e','q','\0'};
constexpr std::uint64_t orderBookTapeTimestampTag = 1ull << 63u;
constexpr std::uint64_t orderBookTapePayloadMask = orderBookTapeTimestampTag - 1u;
constexpr std::int64_t kNsPerSecond = 1000000000ll;

std::int64_t timeframeDurationNs(std::string_view timeframe) noexcept {
    if (timeframe.size() < 2u) return 0;
    std::uint64_t value = 0u;
    std::size_t i = 0u;
    while (i < timeframe.size() && timeframe[i] >= '0' && timeframe[i] <= '9') {
        const std::uint64_t digit = static_cast<std::uint64_t>(timeframe[i] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) return 0;
        value = value * 10u + digit;
        ++i;
    }
    if (value == 0u || i + 1u != timeframe.size()) return 0;
    std::uint64_t unitNs = 0u;
    switch (timeframe[i]) {
        case 's': unitNs = static_cast<std::uint64_t>(kNsPerSecond); break;
        case 'm': unitNs = 60ull * static_cast<std::uint64_t>(kNsPerSecond); break;
        case 'h': unitNs = 60ull * 60ull * static_cast<std::uint64_t>(kNsPerSecond); break;
        case 'd': unitNs = 24ull * 60ull * 60ull * static_cast<std::uint64_t>(kNsPerSecond); break;
        case 'w': unitNs = 7ull * 24ull * 60ull * 60ull * static_cast<std::uint64_t>(kNsPerSecond); break;
        default: return 0;
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / unitNs) return 0;
    return static_cast<std::int64_t>(value * unitNs);
}

std::int64_t tierFromTimeframe(std::string_view timeframe) noexcept {
    if (timeframe == "1m") return 1;
    if (timeframe == "10m") return 2;
    if (timeframe == "15m") return 2;
    if (timeframe == "1d") return 3;
    return 0;
}

std::int64_t durationFromTier(std::int64_t tier) noexcept {
    if (tier == 1) return 60ll * kNsPerSecond;
    if (tier == 2) return 15ll * 60ll * kNsPerSecond;
    if (tier == 3) return 24ll * 60ll * 60ll * kNsPerSecond;
    return 0;
}

bool validateCandle(const CandleRow& row) noexcept {
    if (row.tsNs <= 0 || row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8 || row.quoteAmountE8 < 0) {
        return false;
    }
    if (!row.hasOhlc) return row.tier >= 1 && row.tier <= 3;
    const bool hasNumericTier = row.tier >= 1 && row.tier <= 3;
    const bool hasTextInterval = !row.timeframe.empty() && row.durationNs > 0;
    return (hasNumericTier || hasTextInterval)
        && row.durationNs > 0
        && row.openE8 > 0
        && row.closeE8 > 0
        && row.volumeE8 >= 0;
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
    out = DepthRow{};
    JsonParser parser{tapeLine};
    if (!parser.parseArrayStart()) return false;
    std::uint64_t word = 0;
    if (!parser.parseUInt64(word)) return false;
    if ((word & orderBookTapeTimestampTag) == 0u) return false;
    out.tsNs = static_cast<std::int64_t>(word & orderBookTapePayloadMask);
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
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
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
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseTradeLine(std::string_view line,
                      TradeRow& out,
                      const std::vector<std::string>& aliases) noexcept {
    out = TradeRow{};
    if (aliases.empty()) return parseTradeLine(line, out);

    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    bool firstValue = true;
    for (const auto& alias : aliases) {
        if (!firstValue) {
            if (parser.peek(']')) break;
            if (!parser.parseComma()) return Status::CorruptData;
        }
        if (alias == price) {
            if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
        } else if (alias == amount) {
            if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
        } else if (alias == side) {
            if (!parser.parseInt64(out.side)) return Status::CorruptData;
            if (out.side != 0 && out.side != 1) return Status::CorruptData;
            out.sideBuy = static_cast<std::uint8_t>(out.side);
        } else if (alias == timestamp) {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
        } else if (alias == id) {
            if (!parser.parseUInt64(out.tradeId)) return Status::CorruptData;
        } else if (alias == firstTradeId) {
            if (!parser.parseUInt64(out.firstTradeId)) return Status::CorruptData;
        } else if (alias == lastTradeId) {
            if (!parser.parseUInt64(out.lastTradeId)) return Status::CorruptData;
        } else if (alias == quoteQty) {
            if (!parser.parseInt64(out.quoteQtyE8)) return Status::CorruptData;
        } else if (alias == isBuyerMaker) {
            if (!parseBoolByte(parser, out.isBuyerMaker)) return Status::CorruptData;
        } else if (alias == symbol) {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
        } else if (alias == exchange) {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
        } else if (alias == market) {
            if (!parser.parseString(out.market)) return Status::CorruptData;
        } else if (alias == captureSeq) {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        } else if (alias == ingestSeq) {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        firstValue = false;
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
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
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
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
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseLiquidationLine(std::string_view line,
                            LiquidationRow& out,
                            const std::vector<std::string>& aliases) noexcept {
    out = LiquidationRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    bool firstValue = true;
    for (const auto& alias : aliases) {
        if (!firstValue) {
            if (parser.peek(']')) break;
            if (!parser.parseComma()) return Status::CorruptData;
        }
        if (alias == price) {
            if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
        } else if (alias == amount) {
            if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
        } else if (alias == side) {
            if (!parser.parseInt64(out.side)) return Status::CorruptData;
            if (out.side != 0 && out.side != 1) return Status::CorruptData;
            out.sideBuy = static_cast<std::uint8_t>(out.side);
        } else if (alias == timestamp) {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
        } else if (alias == avgPrice) {
            if (!parser.parseInt64(out.avgPriceE8)) return Status::CorruptData;
        } else if (alias == filledQty) {
            if (!parser.parseInt64(out.filledQtyE8)) return Status::CorruptData;
        } else if (alias == symbol) {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
        } else if (alias == exchange) {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
        } else if (alias == market) {
            if (!parser.parseString(out.market)) return Status::CorruptData;
        } else if (alias == orderType) {
            if (!parser.parseInt64(out.orderType)) return Status::CorruptData;
        } else if (alias == timeInForce) {
            if (!parser.parseInt64(out.timeInForce)) return Status::CorruptData;
        } else if (alias == status) {
            if (!parser.parseInt64(out.status)) return Status::CorruptData;
        } else if (alias == sourceMode) {
            if (!parser.parseInt64(out.sourceMode)) return Status::CorruptData;
        } else if (alias == captureSeq) {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        } else if (alias == ingestSeq) {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        }
        firstValue = false;
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.bidPriceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.bidQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.askPriceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.askQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
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
    if (parser.peek('"')) {
        if (!parser.parseString(out.exchange)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseString(out.market)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseString(out.symbol)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseString(out.timeframe)) return Status::CorruptData;
        out.durationNs = timeframeDurationNs(out.timeframe);
        out.tier = tierFromTimeframe(out.timeframe);
        out.hasOhlc = true;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.openE8)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.highE8)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.lowE8)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.closeE8)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.volumeE8)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.quoteAmountE8)) return Status::CorruptData;
        if (!validateCandle(out)) return Status::CorruptData;
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseInt64(out.tier)) return Status::CorruptData;
    if (out.tier < 1 || out.tier > 3) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    std::int64_t firstPrice = 0;
    if (!parser.parseInt64(firstPrice)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    std::int64_t secondPrice = 0;
    if (!parser.parseInt64(secondPrice)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    std::int64_t thirdPrice = 0;
    if (!parser.parseInt64(thirdPrice)) return Status::CorruptData;
    if (parser.peek(']')) {
        out.highE8 = firstPrice;
        out.lowE8 = secondPrice;
        out.quoteAmountE8 = thirdPrice;
        if (!validateCandle(out)) return Status::CorruptData;
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.closeE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.volumeE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.quoteAmountE8)) return Status::CorruptData;
    out.openE8 = firstPrice;
    out.highE8 = secondPrice;
    out.lowE8 = thirdPrice;
    out.hasOhlc = true;
    out.durationNs = durationFromTier(out.tier);
    if (!validateCandle(out)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseMarkPriceLine(std::string_view line, MarkPriceRow& out) noexcept {
    out = MarkPriceRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.markPriceE8)) return Status::CorruptData;
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
    if (out.tsNs <= 0 || out.buyLimitE8 < 0 || out.sellLimitE8 < 0) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    return parseFlatOrderbook(parser, out.levels, out.tsNs) ? Status::Ok : Status::CorruptData;
}

Status parseDepthLine(std::string_view line,
                      DepthRow& out,
                      const std::vector<std::string>& aliases) noexcept {
    (void)aliases;
    return parseDepthLine(line, out);
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
