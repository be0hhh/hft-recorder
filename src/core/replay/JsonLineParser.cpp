#include "core/replay/JsonLineParser.hpp"

#include <string>
#include <utility>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::replay {

namespace {
using JsonParser = hftrec::json::MiniJsonParser;

bool parsePricePair(JsonParser& parser, PricePair& out) noexcept {
    if (!parser.parseObjectStart()) return false;
    bool sawPrice = false;
    bool sawQty = false;
    std::string key;

    if (parser.peek('}')) return false;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "price_i64") {
            if (!parser.parseInt64(out.priceE8)) return false;
            sawPrice = true;
        } else if (key == "qty_i64") {
            if (!parser.parseInt64(out.qtyE8)) return false;
            sawQty = true;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    return parser.parseObjectEnd() && sawPrice && sawQty;
}

bool parsePairArray(JsonParser& parser, std::vector<PricePair>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        PricePair pair{};
        if (!parsePricePair(parser, pair)) return false;
        out.push_back(std::move(pair));
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawCaptureSeq = false;
    bool sawIngestSeq = false;
    bool sawPrice = false;
    bool sawQty = false;
    bool sawSide = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "tsNs") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "captureSeq") {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        } else if (key == "ingestSeq") {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        } else if (key == "priceE8") {
            if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
            sawPrice = true;
        } else if (key == "qtyE8") {
            if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
            sawQty = true;
        } else if (key == "sideBuy") {
            std::int64_t sideBuy = 0;
            if (!parser.parseInt64(sideBuy)) return Status::CorruptData;
            if (sideBuy != 0 && sideBuy != 1) return Status::CorruptData;
            out.sideBuy = static_cast<std::uint8_t>(sideBuy);
            sawSide = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawPrice || !sawQty || !sawSide) {
        return Status::CorruptData;
    }

    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawBidPrice = false;
    bool sawBidQty = false;
    bool sawAskPrice = false;
    bool sawAskQty = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "tsNs") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "captureSeq") {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        } else if (key == "ingestSeq") {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        } else if (key == "bidPriceE8") {
            if (!parser.parseInt64(out.bidPriceE8)) return Status::CorruptData;
            sawBidPrice = true;
        } else if (key == "bidQtyE8") {
            if (!parser.parseInt64(out.bidQtyE8)) return Status::CorruptData;
            sawBidQty = true;
        } else if (key == "askPriceE8") {
            if (!parser.parseInt64(out.askPriceE8)) return Status::CorruptData;
            sawAskPrice = true;
        } else if (key == "askQtyE8") {
            if (!parser.parseInt64(out.askQtyE8)) return Status::CorruptData;
            sawAskQty = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawBidPrice || !sawBidQty || !sawAskPrice || !sawAskQty) {
        return Status::CorruptData;
    }
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawUpdateId = false;
    bool sawFirstUpdateId = false;
    bool sawBids = false;
    bool sawAsks = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "tsNs") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "captureSeq") {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
        } else if (key == "ingestSeq") {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
        } else if (key == "updateId") {
            if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
            sawUpdateId = true;
        } else if (key == "firstUpdateId") {
            if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
            sawFirstUpdateId = true;
        } else if (key == "bids") {
            if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
            sawBids = true;
        } else if (key == "asks") {
            if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
            sawAsks = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawBids || !sawAsks) {
        return Status::CorruptData;
    }
    if (!sawUpdateId) out.updateId = 0;
    if (!sawFirstUpdateId) out.firstUpdateId = 0;
    return Status::Ok;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawCaptureSeq = false;
    bool sawIngestSeq = false;
    bool sawUpdateId = false;
    bool sawFirstUpdateId = false;
    bool sawSnapshotKind = false;
    bool sawSource = false;
    bool sawExchange = false;
    bool sawMarket = false;
    bool sawSymbol = false;
    bool sawSourceTsNs = false;
    bool sawIngestTsNs = false;
    bool sawAnchorUpdateId = false;
    bool sawAnchorFirstUpdateId = false;
    bool sawTrustedReplayAnchor = false;
    bool sawBids = false;
    bool sawAsks = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "tsNs") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "captureSeq") {
            if (!parser.parseInt64(out.captureSeq)) return Status::CorruptData;
            sawCaptureSeq = true;
        } else if (key == "ingestSeq") {
            if (!parser.parseInt64(out.ingestSeq)) return Status::CorruptData;
            sawIngestSeq = true;
        } else if (key == "updateId") {
            if (!parser.parseInt64(out.updateId)) return Status::CorruptData;
            sawUpdateId = true;
        } else if (key == "firstUpdateId") {
            if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
            sawFirstUpdateId = true;
        } else if (key == "snapshotKind") {
            if (!parser.parseString(out.snapshotKind)) return Status::CorruptData;
            sawSnapshotKind = true;
        } else if (key == "source") {
            if (!parser.parseString(out.source)) return Status::CorruptData;
            sawSource = true;
        } else if (key == "exchange") {
            if (!parser.parseString(out.exchange)) return Status::CorruptData;
            sawExchange = true;
        } else if (key == "market") {
            if (!parser.parseString(out.market)) return Status::CorruptData;
            sawMarket = true;
        } else if (key == "symbol") {
            if (!parser.parseString(out.symbol)) return Status::CorruptData;
            sawSymbol = true;
        } else if (key == "sourceTsNs") {
            if (!parser.parseInt64(out.sourceTsNs)) return Status::CorruptData;
            sawSourceTsNs = true;
        } else if (key == "ingestTsNs") {
            if (!parser.parseInt64(out.ingestTsNs)) return Status::CorruptData;
            sawIngestTsNs = true;
        } else if (key == "anchorUpdateId") {
            if (!parser.parseInt64(out.anchorUpdateId)) return Status::CorruptData;
            sawAnchorUpdateId = true;
        } else if (key == "anchorFirstUpdateId") {
            if (!parser.parseInt64(out.anchorFirstUpdateId)) return Status::CorruptData;
            sawAnchorFirstUpdateId = true;
        } else if (key == "trustedReplayAnchor") {
            std::int64_t trusted = 0;
            if (!parser.parseInt64(trusted)) return Status::CorruptData;
            if (trusted != 0 && trusted != 1) return Status::CorruptData;
            out.trustedReplayAnchor = static_cast<std::uint8_t>(trusted);
            sawTrustedReplayAnchor = true;
        } else if (key == "bids") {
            if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
            sawBids = true;
        } else if (key == "asks") {
            if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
            sawAsks = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawBids || !sawAsks) {
        return Status::CorruptData;
    }
    if (!sawUpdateId) out.updateId = 0;
    if (!sawFirstUpdateId) out.firstUpdateId = 0;
    if (!sawCaptureSeq) out.captureSeq = 0;
    if (!sawIngestSeq) out.ingestSeq = 0;
    if (!sawSnapshotKind) out.snapshotKind.clear();
    if (!sawSource) out.source.clear();
    if (!sawExchange) out.exchange.clear();
    if (!sawMarket) out.market.clear();
    if (!sawSymbol) out.symbol.clear();
    if (!sawSourceTsNs) out.sourceTsNs = 0;
    if (!sawIngestTsNs) out.ingestTsNs = 0;
    if (!sawAnchorUpdateId) out.anchorUpdateId = 0;
    if (!sawAnchorFirstUpdateId) out.anchorFirstUpdateId = 0;
    if (!sawTrustedReplayAnchor) out.trustedReplayAnchor = 0;
    return Status::Ok;
}

}  // namespace hftrec::replay
