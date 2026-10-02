#include "CxetCaptureBridge.hpp"

#include "cxet/Primitives/Composite/Trade.hpp"

namespace hftrec::cxet_bridge {

Status CxetCaptureBridge::initialize() noexcept {return Status::Ok;}
CapturedTradeRow CxetCaptureBridge::captureTrade(const cxet::runtime::market::TradeCommit& trade,
                                                 std::string_view symbol) {
    CapturedTradeRow row{};row.symbol=symbol;row.exchangeId=trade.metadata.exchangeRaw;
    row.tradeId=trade.value.eventId.raw;row.tsNs=trade.value.ts.raw;
    row.priceE8=trade.value.price.raw;row.qtyE8=trade.value.qty.raw;
    row.side=trade.value.initiatorSide==cxet::composite::TradeInitiatorSide::Buyer?1:
        trade.value.initiatorSide==cxet::composite::TradeInitiatorSide::Seller?0:-1;
    row.sideBuy=trade.value.initiatorSide==cxet::composite::TradeInitiatorSide::Buyer;
    row.isBuyerMaker=trade.value.initiatorSide==cxet::composite::TradeInitiatorSide::Seller;
    // The canonical trade carries no aggregate-id span or quote amount; absent
    // native evidence remains zero rather than synthesized from the event id.
    return row;
}
CapturedBookTickerRow CxetCaptureBridge::captureBookTicker(const cxet::runtime::market::BboCommit& value,
                                                           std::string_view symbol) {
    CapturedBookTickerRow row{};row.symbol=symbol;row.exchangeId=value.metadata.exchangeRaw;
    row.eventId=value.value.eventId.raw;row.tsNs=value.value.ts.raw;
    if (value.bidPresent) {row.bidPriceE8=value.value.bid.px.raw;row.bidQtyE8=value.value.bid.qty.raw;}
    if (value.askPresent) {row.askPriceE8=value.value.ask.px.raw;row.askQtyE8=value.value.ask.qty.raw;}
    row.includeBidQty=value.bidPresent;row.includeAskQty=value.askPresent;return row;
}



CapturedOrderBookRow CxetCaptureBridge::captureOrderBook(const cxet::runtime::market::CommitMetadata& metadata,
                                                          const cxet::api::market::PublicMarketDepthFrame& value) {
    CapturedOrderBookRow row{};row.eventId=metadata.nativeIdentity.nativeFirst;row.tsNs=value.exchangeTimestampNs;
    for (std::uint32_t i=0u;i<value.levelCount;++i) {
        const auto& level=value.levels[i];
        const auto qty=level.action==cxet::market::DepthAction::Erase?0:level.quantityRaw;
        if (level.side==cxet::market::DepthSide::Bid) row.bids.push_back({level.priceRaw,qty,0});
        else row.asks.push_back({level.priceRaw,qty,1});
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
