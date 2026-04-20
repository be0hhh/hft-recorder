#include "core/cxet_bridge/CxetCaptureBridge.hpp"

#include <algorithm>

#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/BookTickerRuntimeV1.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/StreamMeta.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/TradeRuntimeV1.hpp"

namespace hftrec::cxet_bridge {

Status CxetCaptureBridge::initialize() noexcept {
    return Status::Ok;
}

CapturedTradeRow CxetCaptureBridge::captureTrade(const cxet::composite::TradePublic& trade) {
    CapturedTradeRow row{};
    row.symbol = trade.symbol.data;
    row.tsNs = static_cast<std::uint64_t>(trade.ts.raw);
    row.priceE8 = static_cast<std::int64_t>(trade.price.raw);
    row.qtyE8 = static_cast<std::int64_t>(trade.amount.raw);
    row.sideBuy = static_cast<std::uint8_t>(trade.side.raw) == 1u;
    return row;
}

CapturedTradeRow CxetCaptureBridge::captureTrade(const cxet::composite::TradeRuntimeV1& trade,
                                                 const cxet::composite::StreamMeta& meta) {
    CapturedTradeRow row{};
    row.symbol = meta.symbol.data;
    row.tsNs = static_cast<std::uint64_t>(trade.ts.raw);
    row.priceE8 = static_cast<std::int64_t>(trade.price.raw);
    row.qtyE8 = static_cast<std::int64_t>(trade.qty.raw);
    row.sideBuy = static_cast<std::uint8_t>(trade.side.raw) == 1u;
    return row;
}

CapturedBookTickerRow CxetCaptureBridge::captureBookTicker(const cxet::composite::BookTickerData& bookTicker,
                                                           const std::vector<std::string>& requestedAliases) {
    CapturedBookTickerRow row{};
    row.symbol = bookTicker.symbol.data;
    row.tsNs = static_cast<std::uint64_t>(bookTicker.ts.raw);
    row.bidPriceE8 = static_cast<std::int64_t>(bookTicker.bidPrice.raw);
    row.askPriceE8 = static_cast<std::int64_t>(bookTicker.askPrice.raw);
    row.bidQtyE8 = static_cast<std::int64_t>(bookTicker.bidAmount.raw);
    row.askQtyE8 = static_cast<std::int64_t>(bookTicker.askAmount.raw);
    row.includeBidQty = std::find(requestedAliases.begin(), requestedAliases.end(), "bidQty") != requestedAliases.end();
    row.includeAskQty = std::find(requestedAliases.begin(), requestedAliases.end(), "askQty") != requestedAliases.end();
    return row;
}

CapturedBookTickerRow CxetCaptureBridge::captureBookTicker(const cxet::composite::BookTickerRuntimeV1& bookTicker,
                                                           const cxet::composite::StreamMeta& meta) {
    CapturedBookTickerRow row{};
    row.symbol = meta.symbol.data;
    row.tsNs = static_cast<std::uint64_t>(bookTicker.ts.raw);
    row.bidPriceE8 = static_cast<std::int64_t>(bookTicker.bidPrice.raw);
    row.askPriceE8 = static_cast<std::int64_t>(bookTicker.askPrice.raw);
    row.bidQtyE8 = static_cast<std::int64_t>(bookTicker.bidQty.raw);
    row.askQtyE8 = static_cast<std::int64_t>(bookTicker.askQty.raw);
    row.includeBidQty = true;
    row.includeAskQty = true;
    return row;
}

CapturedOrderBookRow CxetCaptureBridge::captureOrderBook(const cxet::composite::OrderBookSnapshot& snapshot) {
    CapturedOrderBookRow row{};
    row.symbol = snapshot.symbol.data;
    row.tsNs = static_cast<std::uint64_t>(snapshot.ts.raw);
    row.updateId = static_cast<std::uint64_t>(snapshot.updateId.raw);
    row.firstUpdateId = static_cast<std::uint64_t>(snapshot.firstUpdateId.raw);
    row.bids.reserve(snapshot.bidCount.raw);
    for (std::uint32_t i = 0; i < snapshot.bidCount.raw; ++i) {
        row.bids.push_back(CapturedLevel{
            static_cast<std::int64_t>(snapshot.bids[i].price.raw),
            static_cast<std::int64_t>(snapshot.bids[i].amount.raw)
        });
    }
    row.asks.reserve(snapshot.askCount.raw);
    for (std::uint32_t i = 0; i < snapshot.askCount.raw; ++i) {
        row.asks.push_back(CapturedLevel{
            static_cast<std::int64_t>(snapshot.asks[i].price.raw),
            static_cast<std::int64_t>(snapshot.asks[i].amount.raw)
        });
    }
    return row;
}

CaptureFailureEvent CxetCaptureBridge::makeFailure(CaptureFailureKind kind,
                                                   std::string channel,
                                                   std::string detail,
                                                   bool recoverable) noexcept {
    CaptureFailureEvent event{};
    event.kind = kind;
    event.channel = std::move(channel);
    event.detail = std::move(detail);
    event.recoverable = recoverable;
    return event;
}

}  // namespace hftrec::cxet_bridge
