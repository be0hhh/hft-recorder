#include <gtest/gtest.h>

#include "app/capture_cli.cpp"

namespace hftrec::app {
namespace {

TEST(CaptureCli, DefaultsTradesWarmupToLiveOnly) {
    const auto config = makeDefaultConfig();

    EXPECT_EQ(config.tradesHistoryWarmupSec, 0);
}

TEST(CaptureCli, BookTickerAllUsesFullCryptoVenueSurface) {
    const auto jobs = bookTickerAllJobs();

    ASSERT_EQ(jobs.size(), hftrec::tui::allCryptoVenueSpecs().size());
    ASSERT_EQ(jobs.size(), 31u);

    bool foundPoloniexFutures = false;
    bool foundPoloniexSpot = false;
    bool foundHyperliquidRouteSymbol = false;
    for (const auto& job : jobs) {
        if (job.exchange == "poloniex" && job.market == "futures" && job.symbol == "BTC_USDT_PERP") {
            foundPoloniexFutures = true;
        }
        if (job.exchange == "poloniex" && job.market == "spot" && job.symbol == "BTC_USDT") {
            foundPoloniexSpot = true;
        }
        if (job.exchange == "hyperliquid" && job.market == "futures" && job.symbol == "BTCUSDT" &&
            job.routeSymbol == "BTC") {
            foundHyperliquidRouteSymbol = true;
        }
    }

    EXPECT_TRUE(foundPoloniexFutures);
    EXPECT_TRUE(foundPoloniexSpot);
    EXPECT_TRUE(foundHyperliquidRouteSymbol);
}

}  // namespace
}  // namespace hftrec::app
