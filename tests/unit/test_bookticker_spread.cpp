#include <gtest/gtest.h>

#include "core/arbitrage/BookTickerSpread.hpp"
#include "core/arbitrage/BookTickerSpreadMean.hpp"

namespace {

hftrec::replay::BookTickerRow ticker(std::int64_t tsNs,
                                     std::int64_t bid,
                                     std::int64_t ask) {
    hftrec::replay::BookTickerRow row{};
    row.tsNs = tsNs;
    row.bidPriceE8 = bid;
    row.askPriceE8 = ask;
    row.bidQtyE8 = 1;
    row.askQtyE8 = 1;
    return row;
}

TEST(BookTickerSpread, UsesLastKnownQuotesAndReportsBestDirection) {
    const std::vector<hftrec::replay::BookTickerRow> a{
        ticker(100, 9'990'000'000LL, 10'000'000'000LL),
        ticker(300, 10'100'000'000LL, 10'110'000'000LL),
    };
    const std::vector<hftrec::replay::BookTickerRow> b{
        ticker(200, 10'050'000'000LL, 10'060'000'000LL),
        ticker(400, 10'000'000'000LL, 10'010'000'000LL),
    };

    const auto points = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b);
    ASSERT_EQ(points.size(), 3u);

    EXPECT_EQ(points[0].tsNs, 200);
    EXPECT_EQ(points[0].direction, hftrec::arbitrage::SpreadDirection::BuyAAskSellBBid);
    EXPECT_NEAR(points[0].rawSpreadBps, 50.0, 0.0001);
    EXPECT_NEAR(points[0].internalPenaltyBps, 20.0, 0.0001);
    EXPECT_NEAR(points[0].spreadBps, 50.0, 0.0001);

    EXPECT_EQ(points[1].tsNs, 300);
    EXPECT_EQ(points[1].direction, hftrec::arbitrage::SpreadDirection::BuyBAskSellABid);
    EXPECT_NEAR(points[1].rawSpreadBps, 39.7614, 0.001);
    EXPECT_NEAR(points[1].internalPenaltyBps, 19.8807, 0.001);
    EXPECT_NEAR(points[1].spreadBps, 39.7614, 0.001);
}

TEST(BookTickerSpread, KeepsInternalSpreadsSeparateFromCrossExchangeSpread) {
    const std::vector<hftrec::replay::BookTickerRow> a{
        ticker(100, 9'990'000'000LL, 10'000'000'000LL),
    };
    const std::vector<hftrec::replay::BookTickerRow> b{
        ticker(100, 9'990'000'000LL, 10'000'000'000LL),
    };

    const auto points = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b);
    ASSERT_EQ(points.size(), 1u);

    EXPECT_EQ(points.front().direction, hftrec::arbitrage::SpreadDirection::BuyAAskSellBBid);
    EXPECT_NEAR(points.front().rawSpreadBps, -10.0, 0.0001);
    EXPECT_NEAR(points.front().internalPenaltyBps, 20.0, 0.0001);
    EXPECT_NEAR(points.front().spreadBps, -10.0, 0.0001);
}

TEST(BookTickerSpread, KeepsFeesSeparateFromGrossSpread) {
    const std::vector<hftrec::replay::BookTickerRow> a{
        ticker(100, 9'990'000'000LL, 10'000'000'000LL),
    };
    const std::vector<hftrec::replay::BookTickerRow> b{
        ticker(100, 10'050'000'000LL, 10'060'000'000LL),
    };

    const auto withoutFees = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b, 0.0);
    const auto withFees = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b, 6.0);
    ASSERT_EQ(withoutFees.size(), 1u);
    ASSERT_EQ(withFees.size(), 1u);
    EXPECT_NEAR(withFees.front().feePenaltyBps, 6.0, 0.0001);
    EXPECT_NEAR(withFees.front().spreadBps, withoutFees.front().spreadBps, 0.0001);
}

TEST(BookTickerSpread, SkipsUntilBothSidesHaveValidQuotes) {
    const std::vector<hftrec::replay::BookTickerRow> a{
        ticker(100, 0, 0),
        ticker(150, 10'020'000'000LL, 10'010'000'000LL),
        ticker(200, 10'000'000'000LL, 10'010'000'000LL),
    };
    const std::vector<hftrec::replay::BookTickerRow> b{
        ticker(150, 10'030'000'000LL, 10'040'000'000LL),
    };

    const auto points = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points.front().tsNs, 200);
}

TEST(BookTickerSpread, HoldsLastQuoteAcrossSparseUpdateGap) {
    const std::vector<hftrec::replay::BookTickerRow> a{
        ticker(100'000'000LL, 10'000'000'000LL, 10'010'000'000LL),
    };
    const std::vector<hftrec::replay::BookTickerRow> b{
        ticker(100'000'000LL, 10'030'000'000LL, 10'040'000'000LL),
        ticker(1'200'000'001LL, 10'050'000'000LL, 10'060'000'000LL),
    };

    const auto points = hftrec::arbitrage::buildBestSideBookTickerSpread(a, b);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points.front().tsNs, 100'000'000LL);
    EXPECT_EQ(points.back().tsNs, 1'200'000'001LL);
}


TEST(BookTickerSpread, ComputesTrailingMeanAndCostEdge) {
    std::vector<hftrec::arbitrage::BookTickerSpreadPoint> points{};
    points.push_back(hftrec::arbitrage::BookTickerSpreadPoint{.tsNs = 0, .internalPenaltyBps = 5.0, .spreadBps = -100.0});
    points.push_back(hftrec::arbitrage::BookTickerSpreadPoint{.tsNs = 2'000'000'000LL, .internalPenaltyBps = 10.0, .spreadBps = -80.0});
    points.push_back(hftrec::arbitrage::BookTickerSpreadPoint{.tsNs = 6'000'000'000LL, .internalPenaltyBps = 20.0, .spreadBps = -20.0});

    const auto mean = hftrec::arbitrage::buildRollingBookTickerSpreadMean(points, 5'000'000'000LL, 32.0);
    ASSERT_EQ(mean.size(), 3u);
    EXPECT_NEAR(mean[0].meanBps, -100.0, 0.0001);
    EXPECT_NEAR(mean[1].meanBps, -90.0, 0.0001);
    EXPECT_NEAR(mean[2].meanBps, -50.0, 0.0001);
    EXPECT_NEAR(mean[2].deviationBps, 30.0, 0.0001);
    EXPECT_NEAR(mean[2].costBandBps, 52.0, 0.0001);
    EXPECT_NEAR(mean[2].edgeAfterCostBps, -22.0, 0.0001);
}
}  // namespace

