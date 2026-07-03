#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/tui/RecorderTuiShard.hpp"
#include "core/tui/RecorderTuiSymbols.hpp"

namespace {

hftrec::tui::RecorderTuiJob makeJob(std::string name, std::string symbol) {
    hftrec::tui::RecorderTuiJob job{};
    job.name = std::move(name);
    job.exchange = "binance";
    job.market = "futures";
    job.symbol = std::move(symbol);
    job.channels = hftrec::tui::allLiveChannels();
    return job;
}

}  // namespace

TEST(RecorderTuiShard, SplitsPresetIntoBoundedShardPresets) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.outputDir = "/tmp/recordings";
    preset.progressSec = 7;
    preset.launchWaveSize = 2;
    preset.launchStaggerMs = 300;
    preset.sameExchangeCooldownMs = 900;
    preset.maxActiveJobs = 99;
    preset.jobs = {
        makeJob("btc_binance", "BTCUSDT"),
        makeJob("eth_binance", "ETHUSDT"),
        makeJob("btc_bybit", "BTCUSDT"),
        makeJob("sol_binance", "SOLUSDT"),
        makeJob("eth_bybit", "ETHUSDT"),
    };

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 3, 11);

    ASSERT_EQ(shards.size(), 3u);
    EXPECT_EQ(shards[0].jobs.size(), 2u);
    EXPECT_EQ(shards[1].jobs.size(), 2u);
    EXPECT_EQ(shards[2].jobs.size(), 1u);
    EXPECT_EQ(shards[0].jobs[0].name, "btc_binance");
    EXPECT_EQ(shards[0].jobs[1].name, "btc_bybit");
    EXPECT_EQ(shards[1].jobs[0].name, "eth_binance");
    EXPECT_EQ(shards[1].jobs[1].name, "eth_bybit");
    EXPECT_EQ(shards[2].jobs[0].name, "sol_binance");
    for (const auto& shard : shards) {
        EXPECT_EQ(shard.outputDir, preset.outputDir);
        EXPECT_EQ(shard.progressSec, preset.progressSec);
        EXPECT_EQ(shard.launchWaveSize, preset.launchWaveSize);
        EXPECT_EQ(shard.launchStaggerMs, preset.launchStaggerMs);
        EXPECT_EQ(shard.sameExchangeCooldownMs, preset.sameExchangeCooldownMs);
        EXPECT_EQ(shard.maxActiveJobs, 11);
    }
}

TEST(RecorderTuiShard, KeepsSwapSuffixSymbolVariantsInSameShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = {
        makeJob("btw_linear", "BTWUSDT"),
        makeJob("btw_okx_swap", "BTW-USDT-SWAP"),
        makeJob("btw_poloniex_perp", "BTW_USDT_PERP"),
        makeJob("eth_linear", "ETHUSDT"),
    };

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 2, 7);

    ASSERT_EQ(shards.size(), 2u);
    ASSERT_EQ(shards[0].jobs.size(), 3u);
    EXPECT_EQ(shards[0].jobs[0].name, "btw_linear");
    EXPECT_EQ(shards[0].jobs[1].name, "btw_okx_swap");
    EXPECT_EQ(shards[0].jobs[2].name, "btw_poloniex_perp");
    ASSERT_EQ(shards[1].jobs.size(), 1u);
    EXPECT_EQ(shards[1].jobs[0].name, "eth_linear");
    EXPECT_EQ(shards[0].maxActiveJobs, 7);
    EXPECT_EQ(shards[1].maxActiveJobs, 7);
}

TEST(RecorderTuiShard, KeepsGeneratedHyperliquidRouteSymbolInCanonicalSymbolShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = hftrec::tui::generateJobsForSymbols({"agld"}, hftrec::tui::allCryptoVenueSpecs(), 0);

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 4, 31);

    ASSERT_EQ(shards.size(), 1u);
    ASSERT_EQ(shards[0].jobs.size(), hftrec::tui::allCryptoVenueSpecs().size());
    ASSERT_EQ(shards[0].jobs.size(), 31u);

    bool foundHyperliquid = false;
    bool foundPoloniexFutures = false;
    bool foundPoloniexSpot = false;
    for (const auto& job : shards[0].jobs) {
        if (job.exchange == "hyperliquid" && job.market == "futures") {
            EXPECT_EQ(job.symbol, "AGLDUSDT");
            EXPECT_EQ(job.routeSymbol, "AGLD");
            foundHyperliquid = true;
        }
        if (job.exchange == "poloniex" && job.market == "futures") {
            EXPECT_EQ(job.symbol, "AGLD_USDT_PERP");
            foundPoloniexFutures = true;
        }
        if (job.exchange == "poloniex" && job.market == "spot") {
            EXPECT_EQ(job.symbol, "AGLD_USDT");
            foundPoloniexSpot = true;
        }
    }
    EXPECT_TRUE(foundHyperliquid);
    EXPECT_TRUE(foundPoloniexFutures);
    EXPECT_TRUE(foundPoloniexSpot);
}

TEST(RecorderTuiShard, KeepsEachGeneratedSymbolAsOneFullVenueSurfaceShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = hftrec::tui::generateJobsForSymbols({"lab", "agld", "velvet"},
                                                      hftrec::tui::allCryptoVenueSpecs(),
                                                      0);

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 4, 24);

    ASSERT_EQ(shards.size(), 3u);
    for (const auto& shard : shards) {
        EXPECT_EQ(shard.jobs.size(), hftrec::tui::allCryptoVenueSpecs().size());
        EXPECT_EQ(shard.jobs.size(), 31u);
    }
}

TEST(RecorderTuiShard, CanIsolateEachGeneratedVenueJobIntoOwnShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = hftrec::tui::generateJobsForSymbols({"lab"}, hftrec::tui::allCryptoVenueSpecs(), 0);

    const auto shards = hftrec::tui::splitPresetIntoShards(
        preset,
        31,
        1,
        hftrec::tui::RecorderTuiShardGrouping::ByJob);

    ASSERT_EQ(shards.size(), hftrec::tui::allCryptoVenueSpecs().size());
    ASSERT_EQ(shards.size(), 31u);
    for (const auto& shard : shards) {
        ASSERT_EQ(shard.jobs.size(), 1u);
        EXPECT_EQ(shard.maxActiveJobs, 1);
    }
}

TEST(RecorderTuiShard, SchedulesOnlyMaxActiveQueuedShards) {
    std::vector<hftrec::tui::RecorderTuiShardLaunchState> states(93);

    const auto decision = hftrec::tui::chooseQueuedShardLaunches(states, 8);

    EXPECT_EQ(decision.active, 0);
    EXPECT_EQ(decision.queued, 93);
    ASSERT_EQ(decision.indices.size(), 8u);
    for (std::size_t i = 0; i < decision.indices.size(); ++i) {
        EXPECT_EQ(decision.indices[i], i);
    }
}

TEST(RecorderTuiShard, StartsOneReplacementAfterACompletion) {
    std::vector<hftrec::tui::RecorderTuiShardLaunchState> states(93);
    for (std::size_t i = 0; i < 8u; ++i) states[i].launchStarted = true;
    states[0].exited = true;

    const auto decision = hftrec::tui::chooseQueuedShardLaunches(states, 8);

    EXPECT_EQ(decision.active, 7);
    EXPECT_EQ(decision.queued, 85);
    ASSERT_EQ(decision.indices.size(), 1u);
    EXPECT_EQ(decision.indices.front(), 8u);
}

TEST(RecorderTuiShard, StopRequestPreventsLaunchingQueuedShards) {
    std::vector<hftrec::tui::RecorderTuiShardLaunchState> states(12);
    states[0].launchStarted = true;
    states[1].launchStarted = true;

    hftrec::tui::markQueuedShardLaunchesStopped(states);
    const auto decision = hftrec::tui::chooseQueuedShardLaunches(states, 8);

    EXPECT_TRUE(decision.indices.empty());
    EXPECT_EQ(decision.active, 2);
    EXPECT_EQ(decision.queued, 0);
    for (std::size_t i = 2; i < states.size(); ++i) {
        EXPECT_TRUE(states[i].stopRequested);
        EXPECT_TRUE(states[i].exited);
    }
}

TEST(RecorderTuiShard, DefaultMaxActiveShardsFollowsGroupingSemantics) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.maxActiveJobs = 73;

    EXPECT_EQ(hftrec::tui::defaultRecorderTuiMaxActiveShards(
                  preset,
                  hftrec::tui::RecorderTuiShardGrouping::ByJob,
                  200),
              73);
    EXPECT_EQ(hftrec::tui::defaultRecorderTuiMaxActiveShards(
                  preset,
                  hftrec::tui::RecorderTuiShardGrouping::ByJob,
                  64),
              64);
    EXPECT_EQ(hftrec::tui::defaultRecorderTuiMaxActiveShards(
                  preset,
                  hftrec::tui::RecorderTuiShardGrouping::BySymbol,
                  93),
              93);
    EXPECT_EQ(hftrec::tui::clampRecorderTuiMaxActiveShards(256, 93), 93);
    EXPECT_EQ(hftrec::tui::clampRecorderTuiMaxActiveShards(0, 93), 1);
}
