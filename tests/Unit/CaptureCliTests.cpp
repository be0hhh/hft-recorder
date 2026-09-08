#include <gtest/gtest.h>

#include "CaptureCli.cpp"

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
    bool foundHyperliquidLocalSymbol = false;
    for (const auto& job : jobs) {
        if (job.exchange == "poloniex" && job.market == "futures" && job.symbol == "BTC_USDT") {
            foundPoloniexFutures = true;
        }
        if (job.exchange == "poloniex" && job.market == "spot" && job.symbol == "BTC_USDT") {
            foundPoloniexSpot = true;
        }
        if (job.exchange == "hyperliquid" && job.market == "futures" && job.symbol == "BTC_USDT" &&
            job.routeSymbol.empty()) {
            foundHyperliquidLocalSymbol = true;
        }
    }

    EXPECT_TRUE(foundPoloniexFutures);
    EXPECT_TRUE(foundPoloniexSpot);
    EXPECT_TRUE(foundHyperliquidLocalSymbol);
}

}  // namespace
}  // namespace hftrec::app
