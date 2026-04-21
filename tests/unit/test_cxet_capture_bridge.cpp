#include <gtest/gtest.h>

#include <vector>

#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/BookTickerRuntimeV1.hpp"
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
    const auto publicRow = hftrec::cxet_bridge::CxetCaptureBridge::captureTrade(publicTrade);

    EXPECT_EQ(runtimeRow.symbol, publicRow.symbol);
    EXPECT_EQ(runtimeRow.tsNs, publicRow.tsNs);
    EXPECT_EQ(runtimeRow.priceE8, publicRow.priceE8);
    EXPECT_EQ(runtimeRow.qtyE8, publicRow.qtyE8);
    EXPECT_EQ(runtimeRow.sideBuy, publicRow.sideBuy);
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
    const auto publicRow = hftrec::cxet_bridge::CxetCaptureBridge::captureBookTicker(
        publicBookTicker,
        std::vector<std::string>{"bidQty", "askQty"});

    EXPECT_EQ(runtimeRow.symbol, publicRow.symbol);
    EXPECT_EQ(runtimeRow.tsNs, publicRow.tsNs);
    EXPECT_EQ(runtimeRow.bidPriceE8, publicRow.bidPriceE8);
    EXPECT_EQ(runtimeRow.askPriceE8, publicRow.askPriceE8);
    EXPECT_EQ(runtimeRow.bidQtyE8, publicRow.bidQtyE8);
    EXPECT_EQ(runtimeRow.askQtyE8, publicRow.askQtyE8);
    EXPECT_EQ(runtimeRow.includeBidQty, publicRow.includeBidQty);
    EXPECT_EQ(runtimeRow.includeAskQty, publicRow.includeAskQty);
}

}  // namespace
