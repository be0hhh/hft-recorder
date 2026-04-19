#include "core/capture/JsonSerializers.hpp"

#include <sstream>
#include <array>
#include <algorithm>

#include "core/common/JsonString.hpp"
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

}  // namespace

std::string renderTradeJsonLine(const cxet::composite::TradePublic& trade) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(trade.ts.raw)
        << ",\"priceE8\":" << static_cast<std::int64_t>(trade.price.raw)
        << ",\"qtyE8\":" << static_cast<std::int64_t>(trade.amount.raw)
        << ",\"sideBuy\":" << (static_cast<std::uint8_t>(trade.side.raw) == 1u ? 1 : 0)
        << "}";
    return out.str();
}

std::string renderBookTickerJsonLine(const cxet::composite::BookTickerData& bookTicker,
                                     const std::vector<std::string>& requestedAliases) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(bookTicker.ts.raw)
        << ",\"bidPriceE8\":" << static_cast<std::int64_t>(bookTicker.bidPrice.raw)
        << ",\"askPriceE8\":" << static_cast<std::int64_t>(bookTicker.askPrice.raw);
    if (hasRequestedAlias(requestedAliases, "bidQty")) {
        out << ",\"bidQtyE8\":" << static_cast<std::int64_t>(bookTicker.bidAmount.raw);
    }
    if (hasRequestedAlias(requestedAliases, "askQty")) {
        out << ",\"askQtyE8\":" << static_cast<std::int64_t>(bookTicker.askAmount.raw);
    }
    out
        << "}";
    return out.str();
}

std::string renderDepthJsonLine(const cxet::composite::OrderBookSnapshot& delta) {
    std::ostringstream out;
    out << "{\"tsNs\":" << static_cast<std::uint64_t>(delta.ts.raw)
        << ",\"updateId\":" << static_cast<std::uint64_t>(delta.updateId.raw)
        << ",\"firstUpdateId\":" << static_cast<std::uint64_t>(delta.firstUpdateId.raw)
        << ",\"bids\":";
    appendLevels(out, delta.bids, delta.bidCount.raw);
    out << ",\"asks\":";
    appendLevels(out, delta.asks, delta.askCount.raw);
    out << '}';
    return out.str();
}

std::string renderSnapshotJson(const cxet::composite::OrderBookSnapshot& snapshot) {
    std::ostringstream out;
    out << "{\n"
        << "  \"tsNs\": " << static_cast<std::uint64_t>(snapshot.ts.raw) << ",\n"
        << "  \"updateId\": " << static_cast<std::uint64_t>(snapshot.updateId.raw) << ",\n"
        << "  \"firstUpdateId\": " << static_cast<std::uint64_t>(snapshot.firstUpdateId.raw) << ",\n"
        << "  \"bids\": ";
    appendLevels(out, snapshot.bids, snapshot.bidCount.raw);
    out << ",\n  \"asks\": ";
    appendLevels(out, snapshot.asks, snapshot.askCount.raw);
    out << "\n}\n";
    return out.str();
}

}  // namespace hftrec::capture
