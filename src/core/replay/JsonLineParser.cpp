#include "core/replay/JsonLineParser.hpp"

#include <cstddef>

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

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    return parseFlatOrderbook(parser, out.levels, out.tsNs) ? Status::Ok : Status::CorruptData;
}

}  // namespace hftrec::replay
