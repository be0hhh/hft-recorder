#include "core/cxet_bridge/CxetCaptureBridge.hpp"

#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/BookTickerRuntimeV1.hpp"
#include "primitives/composite/LiquidationEvent.hpp"
#include "primitives/composite/OrderBookDeltaRuntimeV1.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/RuntimeCompatibility.hpp"
#include "primitives/composite/StreamMeta.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/TradeRuntimeV1.hpp"

namespace hftrec::cxet_bridge {

namespace {

CapturedTradeRow makeCapturedTradeRow(const cxet::composite::TradePublic& trade) {
    CapturedTradeRow row{};
    row.symbol = trade.symbol.data;
    row.exchangeId = static_cast<std::uint64_t>(trade.exchangeId.raw);
    row.tradeId = static_cast<std::uint64_t>(trade.id.raw);
    row.tsNs = static_cast<std::uint64_t>(trade.ts.raw);
    row.priceE8 = static_cast<std::int64_t>(trade.price.raw);
    row.qtyE8 = static_cast<std::int64_t>(trade.amount.raw);
    row.firstTradeId = static_cast<std::uint64_t>(trade.firstTradeId.raw);
    row.lastTradeId = static_cast<std::uint64_t>(trade.lastTradeId.raw);
    row.quoteQtyE8 = static_cast<std::int64_t>(trade.quoteAmount.raw);
    row.side = static_cast<std::int64_t>(trade.side.raw);
    row.isBuyerMaker = trade.isBuyerMaker == canon::TriState::True;
    row.sideBuy = static_cast<std::uint8_t>(trade.side.raw) == 1u;
    return row;
}

CapturedBookTickerRow makeCapturedBookTickerRow(const cxet::composite::BookTickerData& bookTicker,
                                                bool includeBidQty,
                                                bool includeAskQty) {
    CapturedBookTickerRow row{};
    row.symbol = bookTicker.symbol.data;
    row.exchangeId = static_cast<std::uint64_t>(bookTicker.exchangeId.raw);
    row.tsNs = static_cast<std::uint64_t>(bookTicker.ts.raw);
    row.bidPriceE8 = static_cast<std::int64_t>(bookTicker.bidPrice.raw);
    row.askPriceE8 = static_cast<std::int64_t>(bookTicker.askPrice.raw);
    row.bidQtyE8 = static_cast<std::int64_t>(bookTicker.bidAmount.raw);
    row.askQtyE8 = static_cast<std::int64_t>(bookTicker.askAmount.raw);
    row.includeBidQty = includeBidQty;
    row.includeAskQty = includeAskQty;
    return row;
}

}  // namespace

Status CxetCaptureBridge::initialize() noexcept {
    return Status::Ok;
}

CapturedTradeRow CxetCaptureBridge::captureTrade(const cxet::composite::TradeRuntimeV1& trade,
                                                 const cxet::composite::StreamMeta& meta) {
    return makeCapturedTradeRow(cxet::composite::compat::materializeTradePublicV1(trade, meta));
}

CapturedBookTickerRow CxetCaptureBridge::captureBookTicker(const cxet::composite::BookTickerRuntimeV1& bookTicker,
                                                           const cxet::composite::StreamMeta& meta) {
    return makeCapturedBookTickerRow(
        cxet::composite::compat::materializeBookTickerDataV1(bookTicker, meta),
        true,
        true);
}

CapturedLiquidationRow CxetCaptureBridge::captureLiquidation(const cxet::composite::LiquidationEvent& event) {
    CapturedLiquidationRow row{};
    row.symbol = event.symbol.data;
    row.exchangeId = static_cast<std::uint64_t>(event.exchangeId.raw);
    row.tsNs = static_cast<std::uint64_t>(event.ts.raw);
    row.priceE8 = static_cast<std::int64_t>(event.price.raw);
    row.qtyE8 = static_cast<std::int64_t>(event.amount.raw);
    row.avgPriceE8 = static_cast<std::int64_t>(event.avgPrice.raw);
    row.filledQtyE8 = static_cast<std::int64_t>(event.filledAmount.raw);
    row.side = static_cast<std::int64_t>(event.side.raw);
    row.sideBuy = static_cast<std::uint8_t>(event.side.raw) == 1u;
    row.orderType = static_cast<std::int64_t>(event.orderType);
    row.timeInForce = static_cast<std::int64_t>(event.timeInForce);
    row.status = static_cast<std::int64_t>(event.orderStatus);
    row.sourceMode = static_cast<std::int64_t>(event.sourceMode);
    return row;
}

CapturedOrderBookRow CxetCaptureBridge::captureOrderBook(const cxet::composite::OrderBookSnapshot& snapshot) {
    CapturedOrderBookRow row{};
    row.symbol = snapshot.symbol.data;
    row.exchangeId = static_cast<std::uint64_t>(snapshot.exchangeId.raw);
    row.tsNs = static_cast<std::uint64_t>(snapshot.ts.raw);
    row.hasUpdateId = snapshot.updateId.raw > 0u;
    row.hasFirstUpdateId = snapshot.firstUpdateId.raw > 0u;
    row.updateId = static_cast<std::uint64_t>(snapshot.updateId.raw);
    row.firstUpdateId = static_cast<std::uint64_t>(snapshot.firstUpdateId.raw);
    row.bids.reserve(snapshot.bidCount.raw);
    for (std::uint32_t i = 0; i < snapshot.bidCount.raw; ++i) {
        row.bids.push_back(CapturedLevel{
            static_cast<std::int64_t>(snapshot.bids[i].price.raw),
            static_cast<std::int64_t>(snapshot.bids[i].amount.raw),
            static_cast<std::int64_t>(snapshot.bids[i].side.raw),
            static_cast<std::uint64_t>(snapshot.bids[i].levelId.raw)
        });
    }
    row.asks.reserve(snapshot.askCount.raw);
    for (std::uint32_t i = 0; i < snapshot.askCount.raw; ++i) {
        row.asks.push_back(CapturedLevel{
            static_cast<std::int64_t>(snapshot.asks[i].price.raw),
            static_cast<std::int64_t>(snapshot.asks[i].amount.raw),
            static_cast<std::int64_t>(snapshot.asks[i].side.raw),
            static_cast<std::uint64_t>(snapshot.asks[i].levelId.raw)
        });
    }
    return row;
}

CapturedOrderBookRow CxetCaptureBridge::captureOrderBook(const cxet::composite::OrderBookDeltaRuntimeV1& delta,
                                                         const cxet::composite::StreamMeta& meta) {
    CapturedOrderBookRow row{};
    row.symbol = meta.symbol.data;
    row.exchangeId = static_cast<std::uint64_t>(meta.exchangeId.raw);
    row.tsNs = static_cast<std::uint64_t>(delta.ts.raw);
    row.hasUpdateId = delta.updateId.raw > 0u;
    row.hasFirstUpdateId = delta.firstUpdateId.raw > 0u;
    row.updateId = static_cast<std::uint64_t>(delta.updateId.raw);
    row.firstUpdateId = static_cast<std::uint64_t>(delta.firstUpdateId.raw);
    row.bids.reserve(delta.bidCount.raw);
    for (std::uint32_t i = 0; i < delta.bidCount.raw; ++i) {
        row.bids.push_back(CapturedLevel{
            static_cast<std::int64_t>(delta.bids[i].px.raw),
            static_cast<std::int64_t>(delta.bids[i].qty.raw),
            0,
            static_cast<std::uint64_t>(i)
        });
    }
    row.asks.reserve(delta.askCount.raw);
    for (std::uint32_t i = 0; i < delta.askCount.raw; ++i) {
        row.asks.push_back(CapturedLevel{
            static_cast<std::int64_t>(delta.asks[i].px.raw),
            static_cast<std::int64_t>(delta.asks[i].qty.raw),
            1,
            static_cast<std::uint64_t>(i)
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
