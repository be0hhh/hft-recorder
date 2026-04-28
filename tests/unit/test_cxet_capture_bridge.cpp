#include <gtest/gtest.h>

#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/BookTickerRuntimeV1.hpp"
#include "primitives/composite/OrderBookDeltaRuntimeV1.hpp"
#include "primitives/composite/RuntimeCompatibility.hpp"
#include "primitives/composite/StreamMeta.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/TradeRuntimeV1.hpp"

namespace {

TEST(CxetCaptureBridge, RuntimeTradeMatchesCompatibilityTradeCapture) {
    cxet::composite::StreamMeta meta{};
    meta.exchangeId.raw = 1u;
    meta.symbol.copyFrom("BTCUSDT");

    cxet::composite::TradeRuntimeV1 runtime{};
    runtime.ts.raw = 1'713'168'000'000'000'123ULL;
    runtime.price.raw = 3'000'100'000'000LL;
    runtime.qty.raw = 15'000'000LL;
    runtime.side = Side::Buy();

    const auto runtimeRow = hftrec::cxet_bridge::CxetCaptureBridge::captureTrade(runtime, meta);
    const auto publicTrade = cxet::composite::compat::materializeTradePublicV1(runtime, meta);

    EXPECT_EQ(runtimeRow.symbol, publicTrade.symbol.data);
    EXPECT_EQ(runtimeRow.tsNs, static_cast<std::uint64_t>(publicTrade.ts.raw));
    EXPECT_EQ(runtimeRow.priceE8, static_cast<std::int64_t>(publicTrade.price.raw));
    EXPECT_EQ(runtimeRow.qtyE8, static_cast<std::int64_t>(publicTrade.amount.raw));
    EXPECT_EQ(runtimeRow.sideBuy, static_cast<std::uint8_t>(publicTrade.side.raw) == 1u);
}

TEST(CxetCaptureBridge, RuntimeBookTickerMatchesCompatibilityBookTickerCapture) {
    cxet::composite::StreamMeta meta{};
    meta.exchangeId.raw = 2u;
    meta.symbol.copyFrom("ETHUSDT");

    cxet::composite::BookTickerRuntimeV1 runtime{};
    runtime.ts.raw = 1'713'168'000'500'000'456ULL;
    runtime.bid.px.raw = 200'000'000'000LL;
    runtime.bid.qty.raw = 50'000'000LL;
    runtime.ask.px.raw = 200'010'000'000LL;
    runtime.ask.qty.raw = 60'000'000LL;

    const auto runtimeRow = hftrec::cxet_bridge::CxetCaptureBridge::captureBookTicker(runtime, meta);
    const auto publicBookTicker = cxet::composite::compat::materializeBookTickerDataV1(runtime, meta);

    EXPECT_EQ(runtimeRow.symbol, publicBookTicker.symbol.data);
    EXPECT_EQ(runtimeRow.tsNs, static_cast<std::uint64_t>(publicBookTicker.ts.raw));
    EXPECT_EQ(runtimeRow.bidPriceE8, static_cast<std::int64_t>(publicBookTicker.bidPrice.raw));
    EXPECT_EQ(runtimeRow.askPriceE8, static_cast<std::int64_t>(publicBookTicker.askPrice.raw));
    EXPECT_EQ(runtimeRow.bidQtyE8, static_cast<std::int64_t>(publicBookTicker.bidAmount.raw));
    EXPECT_EQ(runtimeRow.askQtyE8, static_cast<std::int64_t>(publicBookTicker.askAmount.raw));
    EXPECT_TRUE(runtimeRow.includeBidQty);
    EXPECT_TRUE(runtimeRow.includeAskQty);
}

TEST(CxetCaptureBridge, RuntimeOrderBookRestoresRecorderLevelSemantics) {
    cxet::composite::StreamMeta meta{};
    meta.exchangeId.raw = 1u;
    meta.symbol.copyFrom("BTCUSDT");

    cxet::composite::OrderBookDeltaRuntimeV1 delta{};
    delta.ts.raw = 1'713'168'001'000'000'000ULL;
    delta.updateId.raw = 120u;
    delta.firstUpdateId.raw = 118u;
    delta.bidCount.raw = 1u;
    delta.askCount.raw = 2u;
    delta.bids[0].px.raw = 3'000'000'000'000LL;
    delta.bids[0].qty.raw = 0LL;
    delta.asks[0].px.raw = 3'000'100'000'000LL;
    delta.asks[0].qty.raw = 15'000'000LL;
    delta.asks[1].px.raw = 3'000'200'000'000LL;
    delta.asks[1].qty.raw = 25'000'000LL;

    const auto row = hftrec::cxet_bridge::CxetCaptureBridge::captureOrderBook(delta, meta);

    ASSERT_EQ(row.bids.size(), 1u);
    ASSERT_EQ(row.asks.size(), 2u);
    EXPECT_EQ(row.bids[0].priceI64, 3'000'000'000'000LL);
    EXPECT_EQ(row.bids[0].qtyI64, 0LL);
    EXPECT_EQ(row.bids[0].side, 0);
    EXPECT_EQ(row.asks[1].side, 1);
}

}  // namespace
