#include "core/capture/JsonSerializers.hpp"

#include <sstream>
#include <array>

#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace hftrec::capture {

namespace {

const char* sideToString(const Side& side) noexcept {
    switch (static_cast<std::uint8_t>(side.raw)) {
        case 0: return "sell";
        case 1: return "buy";
        default: return "unknown";
    }
}

const char* triStateToString(canon::TriState v) noexcept {
    return static_cast<std::uint8_t>(v) == 2u ? "null"
        : (static_cast<std::uint8_t>(v) == 1u ? "true" : "false");
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

std::string renderTradeJsonLine(const std::string& sessionId,
                                const std::string& exchange,
                                const std::string& market,
                                const cxet::composite::TradePublic& trade,
                                std::uint64_t eventIndex) {
    std::ostringstream out;
    out << "{\"session_id\":\"" << sessionId
        << "\",\"channel\":\"trades\""
        << ",\"exchange\":\"" << exchange
        << "\",\"market\":\"" << market
        << "\",\"symbol\":\"" << trade.symbol.data
        << "\",\"event_index\":" << eventIndex
        << ",\"event_time_ns\":" << static_cast<std::uint64_t>(trade.ts.raw)
        << ",\"trade_time_ns\":" << static_cast<std::uint64_t>(trade.ts.raw)
        << ",\"trade_id\":" << static_cast<std::uint64_t>(trade.id.raw)
        << ",\"price_i64\":" << static_cast<std::int64_t>(trade.price.raw)
        << ",\"qty_i64\":" << static_cast<std::int64_t>(trade.amount.raw)
        << ",\"side\":\"" << sideToString(trade.side)
        << "\",\"is_aggregated\":true"
        << ",\"is_buyer_maker\":" << triStateToString(trade.isBuyerMaker)
        << "}";
    return out.str();
}

std::string renderBookTickerJsonLine(const std::string& sessionId,
                                     const std::string& exchange,
                                     const std::string& market,
                                     const cxet::composite::BookTickerData& bookTicker,
                                     std::uint64_t eventIndex) {
    std::ostringstream out;
    out << "{\"session_id\":\"" << sessionId
        << "\",\"channel\":\"bookticker\""
        << ",\"exchange\":\"" << exchange
        << "\",\"market\":\"" << market
        << "\",\"symbol\":\"" << bookTicker.symbol.data
        << "\",\"event_index\":" << eventIndex
        << ",\"event_time_ns\":" << static_cast<std::uint64_t>(bookTicker.ts.raw)
        << ",\"update_id\":0"
        << ",\"best_bid_price_i64\":" << static_cast<std::int64_t>(bookTicker.bidPrice.raw)
        << ",\"best_bid_qty_i64\":" << static_cast<std::int64_t>(bookTicker.bidAmount.raw)
        << ",\"best_ask_price_i64\":" << static_cast<std::int64_t>(bookTicker.askPrice.raw)
        << ",\"best_ask_qty_i64\":" << static_cast<std::int64_t>(bookTicker.askAmount.raw)
        << "}";
    return out.str();
}

std::string renderDepthJsonLine(const std::string& sessionId,
                                const std::string& exchange,
                                const std::string& market,
                                const cxet::composite::OrderBookSnapshot& delta,
                                std::uint64_t eventIndex) {
    std::ostringstream out;
    out << "{\"session_id\":\"" << sessionId
        << "\",\"channel\":\"depth\""
        << ",\"exchange\":\"" << exchange
        << "\",\"market\":\"" << market
        << "\",\"symbol\":\"" << delta.symbol.data
        << "\",\"event_index\":" << eventIndex
        << ",\"event_time_ns\":" << static_cast<std::uint64_t>(delta.ts.raw)
        << ",\"first_update_id\":" << static_cast<std::uint64_t>(delta.firstUpdateId.raw)
        << ",\"final_update_id\":" << static_cast<std::uint64_t>(delta.updateId.raw)
        << ",\"bids\":";
    appendLevels(out, delta.bids, delta.bidCount.raw);
    out << ",\"asks\":";
    appendLevels(out, delta.asks, delta.askCount.raw);
    out << '}';
    return out.str();
}

std::string renderSnapshotJson(const std::string& sessionId,
                               const std::string& exchange,
                               const std::string& market,
                               const cxet::composite::OrderBookSnapshot& snapshot,
                               std::uint64_t snapshotIndex) {
    std::ostringstream out;
    out << "{\n"
        << "  \"session_id\": \"" << sessionId << "\",\n"
        << "  \"channel\": \"snapshot\",\n"
        << "  \"exchange\": \"" << exchange << "\",\n"
        << "  \"market\": \"" << market << "\",\n"
        << "  \"symbol\": \"" << snapshot.symbol.data << "\",\n"
        << "  \"snapshot_index\": " << snapshotIndex << ",\n"
        << "  \"snapshot_time_ns\": " << static_cast<std::uint64_t>(snapshot.ts.raw) << ",\n"
        << "  \"bids\": ";
    appendLevels(out, snapshot.bids, snapshot.bidCount.raw);
    out << ",\n  \"asks\": ";
    appendLevels(out, snapshot.asks, snapshot.askCount.raw);
    out << "\n}\n";
    return out.str();
}

}  // namespace hftrec::capture
