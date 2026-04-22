#include "core/replay/JsonLineParser.hpp"

#include <cstddef>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::replay {

namespace {
using JsonParser = hftrec::json::MiniJsonParser;

bool parsePricePair(JsonParser& parser, PricePair& out) noexcept {
    std::int64_t side = 0;
    std::int64_t levelId = 0;
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.qtyE8)) return false;
    if (!parser.parseComma()) return false;
    if (!parser.parseInt64(out.priceE8)) return false;
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

bool parseCountedPairs(JsonParser& parser,
                       std::int64_t declaredCount,
                       std::vector<PricePair>& out) noexcept {
    if (declaredCount < 0) return false;
    if (!parsePairArray(parser, out)) return false;
    return out.size() == static_cast<std::size_t>(declaredCount);
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    std::int64_t exchangeId = 0;
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
    if (!parser.parseInt64(exchangeId)) return Status::CorruptData;
    out.exchangeId = static_cast<std::uint64_t>(exchangeId);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(tradeId)) return Status::CorruptData;
    out.tradeId = static_cast<std::uint64_t>(tradeId);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(isBuyerMaker)) return Status::CorruptData;
    if (isBuyerMaker != 0 && isBuyerMaker != 1) return Status::CorruptData;
    out.isBuyerMaker = static_cast<std::uint8_t>(isBuyerMaker);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(firstTradeId)) return Status::CorruptData;
    out.firstTradeId = static_cast<std::uint64_t>(firstTradeId);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(lastTradeId)) return Status::CorruptData;
    out.lastTradeId = static_cast<std::uint64_t>(lastTradeId);
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.quoteQtyE8)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    std::int64_t exchangeId = 0;
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.bidQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.bidPriceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.askQtyE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.askPriceE8)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(exchangeId)) return Status::CorruptData;
    out.exchangeId = static_cast<std::uint64_t>(exchangeId);
    if (parser.peek(']')) {
        if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
        return Status::Ok;
    }
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    std::int64_t exchangeId = 0;
    std::int64_t bidCount = 0;
    std::int64_t askCount = 0;
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(bidCount)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(askCount)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.peek('[')) {
        if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
    }
    if (!parseCountedPairs(parser, bidCount, out.bids)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parseCountedPairs(parser, askCount, out.asks)) return Status::CorruptData;
    if (!parser.peek(']')) {
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(exchangeId)) return Status::CorruptData;
        out.exchangeId = static_cast<std::uint64_t>(exchangeId);
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    std::int64_t exchangeId = 0;
    std::int64_t bidCount = 0;
    std::int64_t askCount = 0;
    std::int64_t trusted = 0;
    if (!parser.parseArrayStart()) return Status::CorruptData;
    if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(bidCount)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.parseInt64(askCount)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parser.peek('[')) {
        if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.sourceTsNs)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.ingestTsNs)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.anchorUpdateId)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(out.anchorFirstUpdateId)) return Status::CorruptData;
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(trusted)) return Status::CorruptData;
        if (trusted != 0 && trusted != 1) return Status::CorruptData;
        out.trustedReplayAnchor = static_cast<std::uint8_t>(trusted);
        if (!parser.parseComma()) return Status::CorruptData;
    }
    if (!parseCountedPairs(parser, bidCount, out.bids)) return Status::CorruptData;
    if (!parser.parseComma()) return Status::CorruptData;
    if (!parseCountedPairs(parser, askCount, out.asks)) return Status::CorruptData;
    if (!parser.peek(']')) {
        if (!parser.parseComma()) return Status::CorruptData;
        if (!parser.parseInt64(exchangeId)) return Status::CorruptData;
        out.exchangeId = static_cast<std::uint64_t>(exchangeId);
    }
    if (!parser.parseArrayEnd() || !parser.finish()) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec::replay
