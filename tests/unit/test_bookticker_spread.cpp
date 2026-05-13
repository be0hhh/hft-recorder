#include <gtest/gtest.h>

#include "core/arbitrage/BookTickerSpread.hpp"

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
    EXPECT_NEAR(points[0].spreadBps, 30.0, 0.0001);

    EXPECT_EQ(points[1].tsNs, 300);
    EXPECT_EQ(points[1].direction, hftrec::arbitrage::SpreadDirection::BuyBAskSellABid);
    EXPECT_NEAR(points[1].rawSpreadBps, 39.7614, 0.001);
    EXPECT_NEAR(points[1].internalPenaltyBps, 19.8807, 0.001);
    EXPECT_NEAR(points[1].spreadBps, 19.8807, 0.001);
}

TEST(BookTickerSpread, SubtractsInternalSpreadsFromCrossExchangeEdge) {
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
    EXPECT_NEAR(points.front().spreadBps, -30.0, 0.0001);
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

}  // namespace
