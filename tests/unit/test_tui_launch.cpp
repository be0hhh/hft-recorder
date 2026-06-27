#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "core/tui/RecorderTuiLaunch.hpp"
#include "core/tui/RecorderTuiPreset.hpp"

namespace {

using hftrec::tui::LaunchChannel;
using hftrec::tui::RecorderTuiJob;
using hftrec::tui::RecorderTuiPreset;
using hftrec::tui::buildLaunchPlan;
using hftrec::tui::exclusiveMarketDataSessionKey;
using hftrec::tui::filterLaunchJobChannels;
using hftrec::tui::requiresExclusiveMarketDataSession;

bool unavailableGateReference(const RecorderTuiJob& job, LaunchChannel channel, void*) {
    if (job.exchange != "gate") return true;
    return channel != LaunchChannel::Funding && channel != LaunchChannel::PriceLimit;
}

bool unavailableAll(const RecorderTuiJob&, LaunchChannel, void*) {
    return false;
}

RecorderTuiJob job(std::string_view name, std::string_view exchange) {
    RecorderTuiJob out{};
    out.name = std::string{name};
    out.exchange = std::string{exchange};
    out.market = "futures";
    out.symbol = "BTCUSDT";
    out.channels = hftrec::tui::allLiveChannels();
    return out;
}

}  // namespace

TEST(RecorderTuiLaunch, SkipsUnsupportedChannelsButKeepsRunnableJob) {
    RecorderTuiPreset preset{};
    preset.jobs = {job("gate", "gate")};

    const auto plan = buildLaunchPlan(preset, unavailableGateReference, nullptr);

    ASSERT_EQ(plan.jobs.size(), 1u);
    EXPECT_FALSE(plan.jobs.front().skipJob);
    EXPECT_TRUE(plan.jobs.front().job.channels.trades);
    EXPECT_TRUE(plan.jobs.front().job.channels.orderbook);
    EXPECT_FALSE(plan.jobs.front().job.channels.funding);
    EXPECT_FALSE(plan.jobs.front().job.channels.priceLimit);
    EXPECT_TRUE(plan.jobs.front().skippedChannels.funding);
    EXPECT_TRUE(plan.jobs.front().skippedChannels.priceLimit);
    EXPECT_EQ(plan.runnableJobs, 1u);
    EXPECT_EQ(plan.skippedJobs, 0u);
}

TEST(RecorderTuiLaunch, SkipsJobWhenNoChannelsRemain) {
    RecorderTuiPreset preset{};
    preset.jobs = {job("gate", "gate")};

    const auto plan = buildLaunchPlan(preset, unavailableAll, nullptr);

    ASSERT_EQ(plan.jobs.size(), 1u);
    EXPECT_TRUE(plan.jobs.front().skipJob);
    EXPECT_NE(plan.jobs.front().skipReason.find("no supported channels"), std::string::npos);
    EXPECT_EQ(plan.runnableJobs, 0u);
    EXPECT_EQ(plan.skippedJobs, 1u);
}

TEST(RecorderTuiLaunch, FiltersSingleScheduledJobWithoutRescheduling) {
    hftrec::tui::RecorderTuiLaunchJob planned{};
    planned.job = job("gate", "gate");
    planned.originalIndex = 7;
    planned.scheduledStartMs = 1500;

    const auto filtered = filterLaunchJobChannels(planned, unavailableGateReference, nullptr);

    EXPECT_FALSE(filtered.skipJob);
    EXPECT_EQ(filtered.originalIndex, 7u);
    EXPECT_EQ(filtered.scheduledStartMs, 1500);
    EXPECT_TRUE(filtered.job.channels.trades);
    EXPECT_TRUE(filtered.job.channels.orderbook);
    EXPECT_FALSE(filtered.job.channels.funding);
    EXPECT_FALSE(filtered.job.channels.priceLimit);
    EXPECT_TRUE(filtered.skippedChannels.funding);
    EXPECT_TRUE(filtered.skippedChannels.priceLimit);
}

TEST(RecorderTuiLaunch, SchedulesSameExchangeApartAndUsesWaveStart) {
    RecorderTuiPreset preset{};
    preset.launchWaveSize = 2;
    preset.launchStaggerMs = 250;
    preset.sameExchangeCooldownMs = 1500;
    preset.jobs = {
        job("binance_a", "binance"),
        job("binance_b", "binance"),
        job("bybit_a", "bybit"),
        job("okx_a", "okx"),
    };

    const auto plan = buildLaunchPlan(preset, nullptr, nullptr);

    ASSERT_EQ(plan.jobs.size(), 4u);
    EXPECT_EQ(plan.jobs[0].scheduledStartMs, 0);
    EXPECT_EQ(plan.jobs[1].scheduledStartMs, 0);
    EXPECT_EQ(plan.jobs[2].scheduledStartMs, 250);
    EXPECT_GE(plan.jobs[3].scheduledStartMs, 1500);
    EXPECT_EQ(plan.jobs[3].job.exchange, "binance");
}

TEST(RecorderTuiLaunch, MarksBinanceSpotPublicFixMarketDataAsExclusive) {
    RecorderTuiJob spot = job("binance_spot", "Binance");
    spot.market = "SPOT";
    spot.channels = {};
    spot.channels.trades = true;
    spot.channels.bookTicker = true;
    spot.channels.orderbook = true;

    EXPECT_TRUE(requiresExclusiveMarketDataSession(spot));
    EXPECT_EQ(exclusiveMarketDataSessionKey(spot), "binance|spot|market_data_fix");
}

TEST(RecorderTuiLaunch, DoesNotMarkNonBinanceSpotJobsAsExclusive) {
    RecorderTuiJob binanceFutures = job("binance_futures", "binance");
    binanceFutures.market = "futures";

    RecorderTuiJob bybitSpot = job("bybit_spot", "bybit");
    bybitSpot.market = "spot";

    RecorderTuiJob binanceSpotNoPublicFixChannels = job("binance_spot_mark", "binance");
    binanceSpotNoPublicFixChannels.market = "spot";
    binanceSpotNoPublicFixChannels.channels = {};
    binanceSpotNoPublicFixChannels.channels.markPrice = true;

    EXPECT_FALSE(requiresExclusiveMarketDataSession(binanceFutures));
    EXPECT_TRUE(exclusiveMarketDataSessionKey(binanceFutures).empty());
    EXPECT_FALSE(requiresExclusiveMarketDataSession(bybitSpot));
    EXPECT_TRUE(exclusiveMarketDataSessionKey(bybitSpot).empty());
    EXPECT_FALSE(requiresExclusiveMarketDataSession(binanceSpotNoPublicFixChannels));
    EXPECT_TRUE(exclusiveMarketDataSessionKey(binanceSpotNoPublicFixChannels).empty());
}
