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

bool parsePricePair(JsonParser& parser, PricePair& out) noexcept {
    std::int64_t side = 0;
    std::int64_t levelId = 0;
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.priceE8)) return false;
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8)) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(side)) return false;
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(levelId)) return false;
    out.side = side;
    out.levelId = static_cast<std::uint64_t>(levelId);
    return parser.parseArrayEnd();
}

bool parsePairArray(JsonParser& parser, std::vector<PricePair>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        PricePair pair{};
        if (!parsePricePair(parser, pair)) return false;
        out.push_back(pair);
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    std::int64_t tradeId = 0;
    std::int64_t firstTradeId = 0;
    std::int64_t lastTradeId = 0;
    std::int64_t isBuyerMaker = 0;
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
    if (!parser.parseInt64(tradeId)) return Status::CorruptData;
    out.tradeId = static_cast<std::uint64_t>(tradeId);
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(isBuyerMaker)) return Status::CorruptData;
    if (isBuyerMaker != 0 && isBuyerMaker != 1) return Status::CorruptData;
    out.isBuyerMaker = static_cast<std::uint8_t>(isBuyerMaker);
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(firstTradeId)) return Status::CorruptData;
    out.firstTradeId = static_cast<std::uint64_t>(firstTradeId);
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(lastTradeId)) return Status::CorruptData;
    out.lastTradeId = static_cast<std::uint64_t>(lastTradeId);
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.quoteQtyE8)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseTradeLine(std::string_view line,
                      TradeRow& out,
                      const std::vector<std::string>& aliases) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    std::int64_t tradeIdValue = 0;
    std::int64_t firstTradeIdValue = 0;
    std::int64_t lastTradeIdValue = 0;
    std::int64_t isBuyerMakerValue = 0;
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
            if (!parser.parseInt64(tradeIdValue)) return Status::CorruptData;
            out.tradeId = static_cast<std::uint64_t>(tradeIdValue);
        } else if (alias == isBuyerMaker) {
            if (!parser.parseInt64(isBuyerMakerValue)) return Status::CorruptData;
            if (isBuyerMakerValue != 0 && isBuyerMakerValue != 1) return Status::CorruptData;
            out.isBuyerMaker = static_cast<std::uint8_t>(isBuyerMakerValue);
        } else if (alias == firstTradeId) {
            if (!parser.parseInt64(firstTradeIdValue)) return Status::CorruptData;
            out.firstTradeId = static_cast<std::uint64_t>(firstTradeIdValue);
        } else if (alias == lastTradeId) {
            if (!parser.parseInt64(lastTradeIdValue)) return Status::CorruptData;
            out.lastTradeId = static_cast<std::uint64_t>(lastTradeIdValue);
        } else if (alias == quoteQty) {
            if (!parser.parseInt64(out.quoteQtyE8)) return Status::CorruptData;
        } else if (alias == symbol) {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
        } else if (alias == exchange) {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
        } else if (alias == market) {
            if (!parser.parseString(out.market)) return Status::CorruptData;
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
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line,
                           BookTickerRow& out,
                           const std::vector<std::string>& aliases) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    bool firstValue = true;
    for (const auto& alias : aliases) {
        if (!firstValue) {
            if (parser.peek(']')) break;
            if (!parser.parseComma()) return Status::CorruptData;
        }
        if (alias == bidPrice) {
            if (!parser.parseInt64(out.bidPriceE8)) return Status::CorruptData;
        } else if (alias == bidQty) {
            if (!parser.parseInt64(out.bidQtyE8)) return Status::CorruptData;
        } else if (alias == askPrice) {
            if (!parser.parseInt64(out.askPriceE8)) return Status::CorruptData;
        } else if (alias == askQty) {
            if (!parser.parseInt64(out.askQtyE8)) return Status::CorruptData;
        } else if (alias == timestamp) {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
        } else if (alias == symbol) {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
        } else if (alias == exchange) {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
        } else if (alias == market) {
            if (!parser.parseString(out.market)) return Status::CorruptData;
        }
        firstValue = false;
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
    out.hasUpdateId = true;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
    out.hasFirstUpdateId = true;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthLine(std::string_view line,
                      DepthRow& out,
                      const std::vector<std::string>& aliases) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    if (!parser.parseArrayStart()) return Status::CorruptData;
    bool firstValue = true;
    bool parsedBids = false;
    bool parsedAsks = false;
    for (const auto& alias : aliases) {
        const bool isBidGroup = alias == bidPrice || alias == bidQty;
        const bool isAskGroup = alias == askPrice || alias == askQty;
        if (isBidGroup && parsedBids) continue;
        if (isAskGroup && parsedAsks) continue;
        if (!firstValue) {
            if (parser.peek(']')) break;
            if (!parser.parseComma()) return Status::CorruptData;
        }
        if (isBidGroup) {
            if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
            parsedBids = true;
        } else if (isAskGroup) {
            if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
            parsedAsks = true;
        } else if (alias == timestamp) {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
        } else if (alias == symbol) {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
        } else if (alias == exchange) {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
        } else if (alias == market) {
            if (!parser.parseString(out.market)) return Status::CorruptData;
        } else if (alias == updateId) {
            if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
            out.hasUpdateId = true;
        }
        firstValue = false;
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    std::int64_t trusted = 0;
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.symbol)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.exchange)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.market)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
    out.hasUpdateId = true;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
    out.hasFirstUpdateId = true;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.sourceTsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestTsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(trusted)) return Status::CorruptData;
    if (trusted != 0 && trusted != 1) return Status::CorruptData;
    out.trustedReplayAnchor = static_cast<std::uint8_t>(trusted);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.anchorUpdateId)) return Status::CorruptData;
    out.hasAnchorUpdateId = true;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.anchorFirstUpdateId)) return Status::CorruptData;
    out.hasAnchorFirstUpdateId = true;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.snapshotKind)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseString(out.source)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec::replay
