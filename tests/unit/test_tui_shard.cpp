#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "core/tui/RecorderTuiShard.hpp"

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
