#include <gtest/gtest.h>

#include "core/arbitrage/CandleSpread.hpp"
#include "core/arbitrage/PriceBasis.hpp"

namespace {

hftrec::replay::CandleRow candle(std::int64_t tsNs,
                                 std::int64_t close,
                                 std::int64_t durationNs = 60'000'000'000LL) {
    hftrec::replay::CandleRow row{};
    row.tsNs = tsNs;
    row.openE8 = close;
    row.highE8 = close;
    row.lowE8 = close;
    row.closeE8 = close;
    row.durationNs = durationNs;
    row.tier = 1;
    row.hasOhlc = true;
    return row;
}

TEST(CandleSpread, UsesABBestSideDirectionLikeBookTicker) {
    const hftrec::arbitrage::CandleSpreadSource a{
        .rows = {
            candle(100, 10'000'000'000LL),
            candle(200, 10'100'000'000LL),
        },
    };
    const hftrec::arbitrage::CandleSpreadSource b{
        .rows = {
            candle(100, 10'050'000'000LL),
            candle(200, 10'000'000'000LL),
        },
    };

    const auto points = hftrec::arbitrage::buildBestSideCandleSpread(a, b);
    ASSERT_EQ(points.size(), 2u);

    EXPECT_EQ(points[0].direction, hftrec::arbitrage::SpreadDirection::BuyAAskSellBBid);
    EXPECT_EQ(points[0].aCloseE8, 10'000'000'000LL);
    EXPECT_EQ(points[0].bCloseE8, 10'050'000'000LL);
    EXPECT_NEAR(points[0].spreadBps, 50.0, 0.0001);

    EXPECT_EQ(points[1].direction, hftrec::arbitrage::SpreadDirection::BuyBAskSellABid);
    EXPECT_EQ(points[1].aCloseE8, 10'100'000'000LL);
    EXPECT_EQ(points[1].bCloseE8, 10'000'000'000LL);
    EXPECT_NEAR(points[1].spreadBps, 100.0, 0.0001);
}

TEST(CandleSpread, KeepsDurationForGapAwareRendering) {
    const hftrec::arbitrage::CandleSpreadSource a{
        .rows = {
            candle(100, 10'000'000'000LL, 60'000'000'000LL),
            candle(300'000'000'000LL, 10'020'000'000LL, 60'000'000'000LL),
        },
    };
    const hftrec::arbitrage::CandleSpreadSource b{
        .rows = {
            candle(100, 10'050'000'000LL, 60'000'000'000LL),
            candle(300'000'000'000LL, 10'010'000'000LL, 60'000'000'000LL),
        },
    };

    const auto points = hftrec::arbitrage::buildBestSideCandleSpread(a, b);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0].durationNs, 60'000'000'000LL);
    EXPECT_EQ(points[1].durationNs, 60'000'000'000LL);
}

TEST(CandleSpread, EqualClosesHaveNoDirection) {
    const hftrec::arbitrage::CandleSpreadSource a{.rows = {candle(100, 10'000'000'000LL)}};
    const hftrec::arbitrage::CandleSpreadSource b{.rows = {candle(100, 10'000'000'000LL)}};

    const auto points = hftrec::arbitrage::buildBestSideCandleSpread(a, b);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points.front().direction, hftrec::arbitrage::SpreadDirection::None);
    EXPECT_DOUBLE_EQ(points.front().spreadBps, 0.0);
}

TEST(PriceBasis, NormalizesNativeFuturesPriceByBasisQty) {
    EXPECT_EQ(hftrec::arbitrage::normalizeNativePriceE8(3'228'500'000'000LL, 10'000'000'000LL),
              32'285'000'000LL);
    EXPECT_EQ(hftrec::arbitrage::normalizeNativePriceE8(32'222'000'000LL, 100'000'000LL),
              32'222'000'000LL);
    EXPECT_EQ(hftrec::arbitrage::normalizeNativePriceE8(32'222'000'000LL, 0),
              32'222'000'000LL);
}

TEST(CandleSpread, UsesPriceBasisForNativeFuturesComparison) {
    const hftrec::arbitrage::CandleSpreadSource spot{
        .rows = {candle(100, 32'222'000'000LL)},
    };
    const hftrec::arbitrage::CandleSpreadSource futures{
        .rows = {candle(100, 3'228'500'000'000LL)},
        .priceBasisQtyE8 = 10'000'000'000LL,
    };

    const auto points = hftrec::arbitrage::buildBestSideCandleSpread(spot, futures);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points.front().aCloseE8, 32'222'000'000LL);
    EXPECT_EQ(points.front().bCloseE8, 32'285'000'000LL);
    EXPECT_NEAR(points.front().spreadBps, 19.552, 0.01);
}

}  // namespace
