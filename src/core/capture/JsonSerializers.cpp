#include "core/capture/JsonSerializers.hpp"

#include <sstream>

#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::capture {

namespace {

void appendCapturedLevels(std::ostringstream& out, const std::vector<cxet_bridge::CapturedLevel>& levels) {
    out << '[';
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out << ',';
        out << '[' << levels[i].qtyI64
            << ',' << levels[i].priceI64
            << ',' << levels[i].side
            << ',' << levels[i].levelId
            << ']';
    }
    out << ']';
}

}  // namespace

std::string renderTradeJsonLine(const cxet_bridge::CapturedTradeRow& trade,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << '[' << trade.priceE8
        << ',' << trade.qtyE8
        << ',' << trade.side
        << ',' << trade.tsNs
        << ',' << trade.exchangeId
        << ',' << trade.tradeId
        << ',' << (trade.isBuyerMaker ? 1 : 0)
        << ',' << trade.firstTradeId
        << ',' << trade.lastTradeId
        << ',' << trade.quoteQtyE8
        << ',' << sequenceIds.captureSeq
        << ',' << sequenceIds.ingestSeq
        << ']';
    return out.str();
}

std::string renderTradeJsonLine(const replay::TradeRow& trade) {
    std::ostringstream out;
    out << '[' << trade.priceE8
        << ',' << trade.qtyE8
        << ',' << trade.side
        << ',' << trade.tsNs
        << ',' << trade.exchangeId
        << ',' << trade.tradeId
        << ',' << static_cast<int>(trade.isBuyerMaker)
        << ',' << trade.firstTradeId
        << ',' << trade.lastTradeId
        << ',' << trade.quoteQtyE8
        << ',' << trade.captureSeq
        << ',' << trade.ingestSeq
        << ']';
    return out.str();
}

std::string renderBookTickerJsonLine(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                     const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << '[' << (bookTicker.includeBidQty ? bookTicker.bidQtyE8 : 0)
        << ',' << bookTicker.bidPriceE8
        << ',' << (bookTicker.includeAskQty ? bookTicker.askQtyE8 : 0)
        << ',' << bookTicker.askPriceE8
        << ',' << bookTicker.tsNs
        << ',' << bookTicker.exchangeId
        << ',' << sequenceIds.captureSeq
        << ',' << sequenceIds.ingestSeq
        << ']';
    return out.str();
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker) {
    std::ostringstream out;
    out << '[' << bookTicker.bidQtyE8
        << ',' << bookTicker.bidPriceE8
        << ',' << bookTicker.askQtyE8
        << ',' << bookTicker.askPriceE8
        << ',' << bookTicker.tsNs
        << ',' << bookTicker.exchangeId
        << ',' << bookTicker.captureSeq
        << ',' << bookTicker.ingestSeq
        << ']';
    return out.str();
}

std::string renderDepthJsonLine(const cxet_bridge::CapturedOrderBookRow& delta,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << '[' << delta.updateId
        << ',' << delta.firstUpdateId
        << ',' << delta.tsNs
        << ',' << delta.bids.size()
        << ',' << delta.asks.size()
        << ',' << sequenceIds.captureSeq
        << ',' << sequenceIds.ingestSeq
        << ',';
    appendCapturedLevels(out, delta.bids);
    out << ',';
    appendCapturedLevels(out, delta.asks);
    out << ',' << delta.exchangeId << ']';
    return out.str();
}

std::string renderDepthJsonLine(const replay::DepthRow& delta) {
    std::ostringstream out;
    out << '[' << delta.updateId
        << ',' << delta.firstUpdateId
        << ',' << delta.tsNs
        << ',' << delta.bids.size()
        << ',' << delta.asks.size()
        << ',' << delta.captureSeq
        << ',' << delta.ingestSeq
        << ',';
    out << '[';
    for (std::size_t i = 0; i < delta.bids.size(); ++i) {
        if (i != 0) out << ',';
        const auto& level = delta.bids[i];
        out << '[' << level.qtyE8
            << ',' << level.priceE8
            << ',' << level.side
            << ',' << level.levelId
            << ']';
    }
    out << ']';
    out << ',';
    out << '[';
    for (std::size_t i = 0; i < delta.asks.size(); ++i) {
        if (i != 0) out << ',';
        const auto& level = delta.asks[i];
        out << '[' << level.qtyE8
            << ',' << level.priceE8
            << ',' << level.side
            << ',' << level.levelId
            << ']';
    }
    out << ']';
    out << ',' << delta.exchangeId << ']';
    return out.str();
}

std::string renderSnapshotJson(const cxet_bridge::CapturedOrderBookRow& snapshot,
                               const SnapshotProvenance& provenance) {
    std::ostringstream out;
    out << '[' << snapshot.updateId
        << ',' << snapshot.firstUpdateId
        << ',' << snapshot.tsNs
        << ',' << snapshot.bids.size()
        << ',' << snapshot.asks.size()
        << ',' << provenance.sequence.captureSeq
        << ',' << provenance.sequence.ingestSeq
        << ',' << provenance.sourceTsNs
        << ',' << provenance.ingestTsNs
        << ',' << provenance.anchorUpdateId
        << ',' << provenance.anchorFirstUpdateId
        << ',' << (provenance.trustedReplayAnchor ? 1 : 0)
        << ',';
    appendCapturedLevels(out, snapshot.bids);
    out << ',';
    appendCapturedLevels(out, snapshot.asks);
    out << ',' << snapshot.exchangeId << "]\n";
    return out.str();
}

std::string renderSnapshotJson(const replay::SnapshotDocument& snapshot) {
    std::ostringstream out;
    out << '[' << snapshot.updateId
        << ',' << snapshot.firstUpdateId
        << ',' << snapshot.tsNs
        << ',' << snapshot.bids.size()
        << ',' << snapshot.asks.size()
        << ',' << snapshot.captureSeq
        << ',' << snapshot.ingestSeq
        << ',' << snapshot.sourceTsNs
        << ',' << snapshot.ingestTsNs
        << ',' << snapshot.anchorUpdateId
        << ',' << snapshot.anchorFirstUpdateId
        << ',' << static_cast<int>(snapshot.trustedReplayAnchor)
        << ',';
    out << '[';
    for (std::size_t i = 0; i < snapshot.bids.size(); ++i) {
        if (i != 0) out << ',';
        const auto& level = snapshot.bids[i];
        out << '[' << level.qtyE8
            << ',' << level.priceE8
            << ',' << level.side
            << ',' << level.levelId
            << ']';
    }
    out << ']';
    out << ',';
    out << '[';
    for (std::size_t i = 0; i < snapshot.asks.size(); ++i) {
        if (i != 0) out << ',';
        const auto& level = snapshot.asks[i];
        out << '[' << level.qtyE8
            << ',' << level.priceE8
            << ',' << level.side
            << ',' << level.levelId
            << ']';
    }
    out << ']';
    out << ',' << snapshot.exchangeId << ']';
    out << '\n';
    return out.str();
}

}  // namespace hftrec::capture
