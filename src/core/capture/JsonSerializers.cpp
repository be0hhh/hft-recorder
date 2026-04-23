#include "core/capture/JsonSerializers.hpp"

#include <charconv>
#include <system_error>

#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::capture {

namespace {

template <typename Int>
void appendInt(std::string& out, Int value) {
    char buf[32];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc{}) out.append(buf, ptr);
}

void appendCapturedLevels(std::string& out, const std::vector<cxet_bridge::CapturedLevel>& levels) {
    out.push_back('[');
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out.push_back(',');
        out.push_back('[');
        appendInt(out, levels[i].qtyI64);
        out.push_back(',');
        appendInt(out, levels[i].priceI64);
        out.push_back(',');
        appendInt(out, levels[i].side);
        out.push_back(',');
        appendInt(out, levels[i].levelId);
        out.push_back(']');
    }
    out.push_back(']');
}

void appendReplayLevels(std::string& out, const std::vector<replay::PricePair>& levels) {
    out.push_back('[');
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out.push_back(',');
        const auto& level = levels[i];
        out.push_back('[');
        appendInt(out, level.qtyE8);
        out.push_back(',');
        appendInt(out, level.priceE8);
        out.push_back(',');
        appendInt(out, level.side);
        out.push_back(',');
        appendInt(out, level.levelId);
        out.push_back(']');
    }
    out.push_back(']');
}

}  // namespace

std::string renderTradeJsonLine(const cxet_bridge::CapturedTradeRow& trade,
                                const EventSequenceIds& sequenceIds) {
    std::string out;
    out.reserve(192);
    out.push_back('[');
    appendInt(out, trade.priceE8); out.push_back(',');
    appendInt(out, trade.qtyE8); out.push_back(',');
    appendInt(out, trade.side); out.push_back(',');
    appendInt(out, trade.tsNs); out.push_back(',');
    appendInt(out, trade.exchangeId); out.push_back(',');
    appendInt(out, trade.tradeId); out.push_back(',');
    appendInt(out, trade.isBuyerMaker ? 1 : 0); out.push_back(',');
    appendInt(out, trade.firstTradeId); out.push_back(',');
    appendInt(out, trade.lastTradeId); out.push_back(',');
    appendInt(out, trade.quoteQtyE8); out.push_back(',');
    appendInt(out, sequenceIds.captureSeq); out.push_back(',');
    appendInt(out, sequenceIds.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderTradeJsonLine(const replay::TradeRow& trade) {
    std::string out;
    out.reserve(192);
    out.push_back('[');
    appendInt(out, trade.priceE8); out.push_back(',');
    appendInt(out, trade.qtyE8); out.push_back(',');
    appendInt(out, trade.side); out.push_back(',');
    appendInt(out, trade.tsNs); out.push_back(',');
    appendInt(out, trade.exchangeId); out.push_back(',');
    appendInt(out, trade.tradeId); out.push_back(',');
    appendInt(out, static_cast<int>(trade.isBuyerMaker)); out.push_back(',');
    appendInt(out, trade.firstTradeId); out.push_back(',');
    appendInt(out, trade.lastTradeId); out.push_back(',');
    appendInt(out, trade.quoteQtyE8); out.push_back(',');
    appendInt(out, trade.captureSeq); out.push_back(',');
    appendInt(out, trade.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                     const EventSequenceIds& sequenceIds) {
    std::string out;
    out.reserve(144);
    out.push_back('[');
    appendInt(out, bookTicker.includeBidQty ? bookTicker.bidQtyE8 : 0); out.push_back(',');
    appendInt(out, bookTicker.bidPriceE8); out.push_back(',');
    appendInt(out, bookTicker.includeAskQty ? bookTicker.askQtyE8 : 0); out.push_back(',');
    appendInt(out, bookTicker.askPriceE8); out.push_back(',');
    appendInt(out, bookTicker.tsNs); out.push_back(',');
    appendInt(out, bookTicker.exchangeId); out.push_back(',');
    appendInt(out, sequenceIds.captureSeq); out.push_back(',');
    appendInt(out, sequenceIds.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker) {
    std::string out;
    out.reserve(144);
    out.push_back('[');
    appendInt(out, bookTicker.bidQtyE8); out.push_back(',');
    appendInt(out, bookTicker.bidPriceE8); out.push_back(',');
    appendInt(out, bookTicker.askQtyE8); out.push_back(',');
    appendInt(out, bookTicker.askPriceE8); out.push_back(',');
    appendInt(out, bookTicker.tsNs); out.push_back(',');
    appendInt(out, bookTicker.exchangeId); out.push_back(',');
    appendInt(out, bookTicker.captureSeq); out.push_back(',');
    appendInt(out, bookTicker.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderDepthJsonLine(const cxet_bridge::CapturedOrderBookRow& delta,
                                const EventSequenceIds& sequenceIds) {
    std::string out;
    out.reserve(160 + (delta.bids.size() + delta.asks.size()) * 64);
    out.push_back('[');
    appendInt(out, delta.updateId); out.push_back(',');
    appendInt(out, delta.firstUpdateId); out.push_back(',');
    appendInt(out, delta.tsNs); out.push_back(',');
    appendInt(out, delta.bids.size()); out.push_back(',');
    appendInt(out, delta.asks.size()); out.push_back(',');
    appendInt(out, sequenceIds.captureSeq); out.push_back(',');
    appendInt(out, sequenceIds.ingestSeq); out.push_back(',');
    appendCapturedLevels(out, delta.bids);
    out.push_back(',');
    appendCapturedLevels(out, delta.asks);
    out.push_back(',');
    appendInt(out, delta.exchangeId);
    out.push_back(']');
    return out;
}

std::string renderDepthJsonLine(const replay::DepthRow& delta) {
    std::string out;
    out.reserve(160 + (delta.bids.size() + delta.asks.size()) * 64);
    out.push_back('[');
    appendInt(out, delta.updateId); out.push_back(',');
    appendInt(out, delta.firstUpdateId); out.push_back(',');
    appendInt(out, delta.tsNs); out.push_back(',');
    appendInt(out, delta.bids.size()); out.push_back(',');
    appendInt(out, delta.asks.size()); out.push_back(',');
    appendInt(out, delta.captureSeq); out.push_back(',');
    appendInt(out, delta.ingestSeq); out.push_back(',');
    appendReplayLevels(out, delta.bids);
    out.push_back(',');
    appendReplayLevels(out, delta.asks);
    out.push_back(',');
    appendInt(out, delta.exchangeId);
    out.push_back(']');
    return out;
}

std::string renderSnapshotJson(const cxet_bridge::CapturedOrderBookRow& snapshot,
                               const SnapshotProvenance& provenance) {
    std::string out;
    out.reserve(224 + (snapshot.bids.size() + snapshot.asks.size()) * 64);
    out.push_back('[');
    appendInt(out, snapshot.updateId); out.push_back(',');
    appendInt(out, snapshot.firstUpdateId); out.push_back(',');
    appendInt(out, snapshot.tsNs); out.push_back(',');
    appendInt(out, snapshot.bids.size()); out.push_back(',');
    appendInt(out, snapshot.asks.size()); out.push_back(',');
    appendInt(out, provenance.sequence.captureSeq); out.push_back(',');
    appendInt(out, provenance.sequence.ingestSeq); out.push_back(',');
    appendInt(out, provenance.sourceTsNs); out.push_back(',');
    appendInt(out, provenance.ingestTsNs); out.push_back(',');
    appendInt(out, provenance.anchorUpdateId); out.push_back(',');
    appendInt(out, provenance.anchorFirstUpdateId); out.push_back(',');
    appendInt(out, provenance.trustedReplayAnchor ? 1 : 0); out.push_back(',');
    appendCapturedLevels(out, snapshot.bids);
    out.push_back(',');
    appendCapturedLevels(out, snapshot.asks);
    out.push_back(',');
    appendInt(out, snapshot.exchangeId);
    out.push_back(']');
    out.push_back('\n');
    return out;
}

std::string renderSnapshotJson(const replay::SnapshotDocument& snapshot) {
    std::string out;
    out.reserve(224 + (snapshot.bids.size() + snapshot.asks.size()) * 64);
    out.push_back('[');
    appendInt(out, snapshot.updateId); out.push_back(',');
    appendInt(out, snapshot.firstUpdateId); out.push_back(',');
    appendInt(out, snapshot.tsNs); out.push_back(',');
    appendInt(out, snapshot.bids.size()); out.push_back(',');
    appendInt(out, snapshot.asks.size()); out.push_back(',');
    appendInt(out, snapshot.captureSeq); out.push_back(',');
    appendInt(out, snapshot.ingestSeq); out.push_back(',');
    appendInt(out, snapshot.sourceTsNs); out.push_back(',');
    appendInt(out, snapshot.ingestTsNs); out.push_back(',');
    appendInt(out, snapshot.anchorUpdateId); out.push_back(',');
    appendInt(out, snapshot.anchorFirstUpdateId); out.push_back(',');
    appendInt(out, static_cast<int>(snapshot.trustedReplayAnchor)); out.push_back(',');
    appendReplayLevels(out, snapshot.bids);
    out.push_back(',');
    appendReplayLevels(out, snapshot.asks);
    out.push_back(',');
    appendInt(out, snapshot.exchangeId);
    out.push_back(']');
    out.push_back('\n');
    return out;
}

}  // namespace hftrec::capture
