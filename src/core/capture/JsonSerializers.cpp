#include "core/capture/JsonSerializers.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include "core/common/JsonString.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace hftrec::capture {

namespace {

bool hasRequestedAlias(const std::vector<std::string>& requestedAliases, std::string_view alias) {
    return std::any_of(requestedAliases.begin(), requestedAliases.end(),
                       [alias](const std::string& candidate) noexcept {
                           return candidate == alias;
                       });
}

void appendLevels(std::ostringstream& out,
                  const std::array<cxet::composite::OrderBookLevel, rawdata::kMaxOrderbookLevels>& levels,
                  std::uint32_t count) {
    out << '[';
    for (std::uint32_t i = 0; i < count; ++i) {
        if (i != 0) out << ',';
        out << "{\"price_i64\":" << static_cast<std::int64_t>(levels[i].price.raw)
            << ",\"qty_i64\":" << static_cast<std::int64_t>(levels[i].amount.raw)
            << '}';
    }
    out << ']';
}

void appendCapturedLevels(std::ostringstream& out, const std::vector<cxet_bridge::CapturedLevel>& levels) {
    out << '[';
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"price_i64\":" << levels[i].priceI64
            << ",\"qty_i64\":" << levels[i].qtyI64
            << '}';
    }
    out << ']';
}

}  // namespace

std::string renderTradeJsonLine(const cxet::composite::TradePublic& trade,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(trade.ts.raw)
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"priceE8\":" << static_cast<std::int64_t>(trade.price.raw)
        << ",\"qtyE8\":" << static_cast<std::int64_t>(trade.amount.raw)
        << ",\"sideBuy\":" << (static_cast<std::uint8_t>(trade.side.raw) == 1u ? 1 : 0)
        << "}";
    return out.str();
}

std::string renderTradeJsonLine(const cxet_bridge::CapturedTradeRow& trade,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << trade.tsNs
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"priceE8\":" << trade.priceE8
        << ",\"qtyE8\":" << trade.qtyE8
        << ",\"sideBuy\":" << (trade.sideBuy ? 1 : 0)
        << "}";
    return out.str();
}

std::string renderTradeJsonLine(const cxet::composite::TradePublic& trade) {
    return renderTradeJsonLine(trade, {});
}

std::string renderBookTickerJsonLine(const cxet::composite::BookTickerData& bookTicker,
                                     const std::vector<std::string>& requestedAliases,
                                     const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(bookTicker.ts.raw)
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"bidPriceE8\":" << static_cast<std::int64_t>(bookTicker.bidPrice.raw)
        << ",\"askPriceE8\":" << static_cast<std::int64_t>(bookTicker.askPrice.raw);
    if (hasRequestedAlias(requestedAliases, "bidQty")) {
        out << ",\"bidQtyE8\":" << static_cast<std::int64_t>(bookTicker.bidAmount.raw);
    }
    if (hasRequestedAlias(requestedAliases, "askQty")) {
        out << ",\"askQtyE8\":" << static_cast<std::int64_t>(bookTicker.askAmount.raw);
    }
    out << "}";
    return out.str();
}

std::string renderBookTickerJsonLine(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                     const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << bookTicker.tsNs
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"bidPriceE8\":" << bookTicker.bidPriceE8
        << ",\"askPriceE8\":" << bookTicker.askPriceE8;
    if (bookTicker.includeBidQty) out << ",\"bidQtyE8\":" << bookTicker.bidQtyE8;
    if (bookTicker.includeAskQty) out << ",\"askQtyE8\":" << bookTicker.askQtyE8;
    out << "}";
    return out.str();
}

std::string renderBookTickerJsonLine(const cxet::composite::BookTickerData& bookTicker,
                                     const std::vector<std::string>& requestedAliases) {
    return renderBookTickerJsonLine(bookTicker, requestedAliases, {});
}

std::string renderDepthJsonLine(const cxet::composite::OrderBookSnapshot& delta,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(delta.ts.raw)
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"updateId\":" << static_cast<std::uint64_t>(delta.updateId.raw)
        << ",\"firstUpdateId\":" << static_cast<std::uint64_t>(delta.firstUpdateId.raw)
        << ",\"bids\":";
    appendLevels(out, delta.bids, delta.bidCount.raw);
    out << ",\"asks\":";
    appendLevels(out, delta.asks, delta.askCount.raw);
    out << '}';
    return out.str();
}

std::string renderDepthJsonLine(const cxet_bridge::CapturedOrderBookRow& delta,
                                const EventSequenceIds& sequenceIds) {
    std::ostringstream out;
    out << "{\"tsNs\":" << delta.tsNs
        << ",\"captureSeq\":" << sequenceIds.captureSeq
        << ",\"ingestSeq\":" << sequenceIds.ingestSeq
        << ",\"updateId\":" << delta.updateId
        << ",\"firstUpdateId\":" << delta.firstUpdateId
        << ",\"bids\":";
    appendCapturedLevels(out, delta.bids);
    out << ",\"asks\":";
    appendCapturedLevels(out, delta.asks);
    out << '}';
    return out.str();
}

std::string renderDepthJsonLine(const cxet::composite::OrderBookSnapshot& delta) {
    return renderDepthJsonLine(delta, {});
}

std::string renderSnapshotJson(const cxet::composite::OrderBookSnapshot& snapshot,
                               const SnapshotProvenance& provenance) {
    std::ostringstream out;
    out << "{\n"
        << "  \"tsNs\": " << static_cast<std::uint64_t>(snapshot.ts.raw) << ",\n"
        << "  \"captureSeq\": " << provenance.sequence.captureSeq << ",\n"
        << "  \"ingestSeq\": " << provenance.sequence.ingestSeq << ",\n"
        << "  \"updateId\": " << static_cast<std::uint64_t>(snapshot.updateId.raw) << ",\n"
        << "  \"firstUpdateId\": " << static_cast<std::uint64_t>(snapshot.firstUpdateId.raw) << ",\n"
        << "  \"snapshotKind\": " << json::quote(provenance.snapshotKind) << ",\n"
        << "  \"source\": " << json::quote(provenance.source) << ",\n"
        << "  \"exchange\": " << json::quote(provenance.exchange) << ",\n"
        << "  \"market\": " << json::quote(provenance.market) << ",\n"
        << "  \"symbol\": " << json::quote(provenance.symbol) << ",\n"
        << "  \"sourceTsNs\": " << provenance.sourceTsNs << ",\n"
        << "  \"ingestTsNs\": " << provenance.ingestTsNs << ",\n"
        << "  \"anchorUpdateId\": " << provenance.anchorUpdateId << ",\n"
        << "  \"anchorFirstUpdateId\": " << provenance.anchorFirstUpdateId << ",\n"
        << "  \"trustedReplayAnchor\": " << (provenance.trustedReplayAnchor ? 1 : 0) << ",\n"
        << "  \"bids\": ";
    appendLevels(out, snapshot.bids, snapshot.bidCount.raw);
    out << ",\n  \"asks\": ";
    appendLevels(out, snapshot.asks, snapshot.askCount.raw);
    out << "\n}\n";
    return out.str();
}

std::string renderSnapshotJson(const cxet_bridge::CapturedOrderBookRow& snapshot,
                               const SnapshotProvenance& provenance) {
    std::ostringstream out;
    out << "{\n"
        << "  \"tsNs\": " << snapshot.tsNs << ",\n"
        << "  \"captureSeq\": " << provenance.sequence.captureSeq << ",\n"
        << "  \"ingestSeq\": " << provenance.sequence.ingestSeq << ",\n"
        << "  \"updateId\": " << snapshot.updateId << ",\n"
        << "  \"firstUpdateId\": " << snapshot.firstUpdateId << ",\n"
        << "  \"snapshotKind\": " << json::quote(provenance.snapshotKind) << ",\n"
        << "  \"source\": " << json::quote(provenance.source) << ",\n"
        << "  \"exchange\": " << json::quote(provenance.exchange) << ",\n"
        << "  \"market\": " << json::quote(provenance.market) << ",\n"
        << "  \"symbol\": " << json::quote(provenance.symbol) << ",\n"
        << "  \"sourceTsNs\": " << provenance.sourceTsNs << ",\n"
        << "  \"ingestTsNs\": " << provenance.ingestTsNs << ",\n"
        << "  \"anchorUpdateId\": " << provenance.anchorUpdateId << ",\n"
        << "  \"anchorFirstUpdateId\": " << provenance.anchorFirstUpdateId << ",\n"
        << "  \"trustedReplayAnchor\": " << (provenance.trustedReplayAnchor ? 1 : 0) << ",\n"
        << "  \"bids\": ";
    appendCapturedLevels(out, snapshot.bids);
    out << ",\n  \"asks\": ";
    appendCapturedLevels(out, snapshot.asks);
    out << "\n}\n";
    return out.str();
}

std::string renderSnapshotJson(const cxet::composite::OrderBookSnapshot& snapshot) {
    SnapshotProvenance provenance{};
    return renderSnapshotJson(snapshot, provenance);
}

}  // namespace hftrec::capture
