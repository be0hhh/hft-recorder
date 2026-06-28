#include <gtest/gtest.h>

#include <string>
#include <utility>

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
        makeJob("eth_linear", "ETHUSDT"),
    };

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 2, 7);

    ASSERT_EQ(shards.size(), 2u);
    ASSERT_EQ(shards[0].jobs.size(), 2u);
    EXPECT_EQ(shards[0].jobs[0].name, "btw_linear");
    EXPECT_EQ(shards[0].jobs[1].name, "btw_okx_swap");
    ASSERT_EQ(shards[1].jobs.size(), 1u);
    EXPECT_EQ(shards[1].jobs[0].name, "eth_linear");
    EXPECT_EQ(shards[0].maxActiveJobs, 7);
    EXPECT_EQ(shards[1].maxActiveJobs, 7);
}

TEST(RecorderTuiShard, KeepsGeneratedHyperliquidRouteSymbolInCanonicalSymbolShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = hftrec::tui::generateJobsForSymbols({"agld"}, hftrec::tui::allCryptoVenueSpecs(), 0);

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 4, 29);

    ASSERT_EQ(shards.size(), 1u);
    ASSERT_EQ(shards[0].jobs.size(), 29u);

    bool foundHyperliquid = false;
    for (const auto& job : shards[0].jobs) {
        if (job.exchange == "hyperliquid" && job.market == "futures") {
            EXPECT_EQ(job.symbol, "AGLDUSDT");
            EXPECT_EQ(job.routeSymbol, "AGLD");
            foundHyperliquid = true;
        }
    }
    EXPECT_TRUE(foundHyperliquid);
}

TEST(RecorderTuiShard, KeepsEachGeneratedSymbolAsTwentyNineJobShard) {
    hftrec::tui::RecorderTuiPreset preset{};
    preset.jobs = hftrec::tui::generateJobsForSymbols({"lab", "agld", "velvet"},
                                                      hftrec::tui::allCryptoVenueSpecs(),
                                                      0);

    const auto shards = hftrec::tui::splitPresetIntoShards(preset, 4, 24);

    ASSERT_EQ(shards.size(), 3u);
    for (const auto& shard : shards) {
        EXPECT_EQ(shard.jobs.size(), 29u);
    }
}
