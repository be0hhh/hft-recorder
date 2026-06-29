#include <gtest/gtest.h>

#include "gui/viewer/MoexBasisSeries.hpp"

namespace {

constexpr std::int64_t kScale = 100000000LL;

hftrec::replay::CandleRow candle(std::int64_t tsNs, std::int64_t closeE8) {
    hftrec::replay::CandleRow row{};
    row.tsNs = tsNs;
    row.openE8 = closeE8;
    row.highE8 = closeE8;
    row.lowE8 = closeE8;
    row.closeE8 = closeE8;
    row.durationNs = 60LL * 1000000000LL;
    row.hasOhlc = true;
    return row;
}

}  // namespace

TEST(MoexBasisSeries, BuildsBasisFromAlignedClosedCandlesAndPriceBasis) {
    hftrec::gui::viewer::MoexBasisLegSeries spot{};
    spot.candles = {
        candle(1000, 100 * kScale),
        candle(2000, 101 * kScale),
    };

    hftrec::gui::viewer::MoexBasisLegSeries future{};
    future.expiryUtcNs = 3000;
    future.priceBasisQtyE8 = 2 * kScale;
    future.candles = {
        candle(1000, 202 * kScale),
        candle(2000, 204 * kScale),
    };

    const auto points = hftrec::gui::viewer::buildMoexBasisPoints(spot, future);

    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0].futureCloseE8, 101 * kScale);
    EXPECT_DOUBLE_EQ(points[0].basisBps, 100.0);
    EXPECT_NEAR(points[1].basisBps, 99.0099009901, 0.0001);
}

TEST(MoexBasisSeries, RejectsFutureWithoutExpiryOrPriceBasisMetadata) {
    hftrec::gui::viewer::MoexBasisLegSeries spot{};
    spot.candles = {candle(1000, 100 * kScale)};

    hftrec::gui::viewer::MoexBasisLegSeries future{};
    future.candles = {candle(1000, 101 * kScale)};

    EXPECT_TRUE(hftrec::gui::viewer::buildMoexBasisPoints(spot, future).empty());

    future.expiryUtcNs = 3000;
    EXPECT_TRUE(hftrec::gui::viewer::buildMoexBasisPoints(spot, future).empty());

    future.priceBasisQtyE8 = kScale;
    EXPECT_FALSE(hftrec::gui::viewer::buildMoexBasisPoints(spot, future).empty());
}
