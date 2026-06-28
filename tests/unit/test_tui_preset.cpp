#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/recordings/RecordingRoot.hpp"
#include "core/tui/RecorderTuiPreset.hpp"

namespace {

using hftrec::tui::ChannelSelection;
using hftrec::tui::RecorderTuiPreset;
using hftrec::tui::parseChannelSelection;
using hftrec::tui::parseDurationMinutes;
using hftrec::tui::parsePresetText;
using hftrec::tui::defaultPresetPath;
using hftrec::tui::renderChannelSelection;
using hftrec::tui::renderPresetText;
using hftrec::tui::resolvePresetPath;

TEST(RecorderTuiPreset, ParsesDurationMinutesAndIndefiniteAliases) {
    std::int64_t minutes = -1;
    std::string error;

    EXPECT_TRUE(parseDurationMinutes("30", minutes, error));
    EXPECT_EQ(minutes, 30);

    EXPECT_TRUE(parseDurationMinutes("45m", minutes, error));
    EXPECT_EQ(minutes, 45);

    EXPECT_TRUE(parseDurationMinutes("", minutes, error));
    EXPECT_EQ(minutes, 0);

    EXPECT_TRUE(parseDurationMinutes("none", minutes, error));
    EXPECT_EQ(minutes, 0);

    EXPECT_FALSE(parseDurationMinutes("-1", minutes, error));
    EXPECT_FALSE(error.empty());
}

TEST(RecorderTuiPreset, ParsesAllChannelsAndCanonicalRenders) {
    ChannelSelection channels{};
    std::string error;

    ASSERT_TRUE(parseChannelSelection("all", channels, error));
    EXPECT_TRUE(channels.trades);
    EXPECT_TRUE(channels.liquidations);
    EXPECT_TRUE(channels.bookTicker);
    EXPECT_TRUE(channels.orderbook);
    EXPECT_TRUE(channels.markPrice);
    EXPECT_TRUE(channels.indexPrice);
    EXPECT_TRUE(channels.funding);
    EXPECT_TRUE(channels.priceLimit);
    EXPECT_EQ(renderChannelSelection(channels),
              "trades,liquidations,bookticker,orderbook,mark_price,index_price,funding,price_limit");
}

TEST(RecorderTuiPreset, ParsesChannelAliasesAndRejectsUnknownNames) {
    ChannelSelection channels{};
    std::string error;

    ASSERT_TRUE(parseChannelSelection("trades,bookticker,mark,index,price-limit", channels, error));
    EXPECT_TRUE(channels.trades);
    EXPECT_TRUE(channels.bookTicker);
    EXPECT_TRUE(channels.markPrice);
    EXPECT_TRUE(channels.indexPrice);
    EXPECT_TRUE(channels.priceLimit);
    EXPECT_FALSE(channels.orderbook);
    EXPECT_FALSE(channels.funding);

    EXPECT_FALSE(parseChannelSelection("trades,unknown_channel", channels, error));
    EXPECT_NE(error.find("unknown_channel"), std::string::npos);
}

TEST(RecorderTuiPreset, ParsesMultipleJobsFromIniLikePreset) {
    constexpr std::string_view text = R"(
output_dir=/mnt/d/cxet-recordings
progress_sec=10

[job binance_btc]
exchange=binance
market=futures
symbol=BTCUSDT
route_symbol=BTC
duration_min=0
channels=all

[job bybit_eth]
exchange=bybit
market=futures
symbol=ETHUSDT
duration_min=30
channels=trades,bookticker,orderbook
)";

    RecorderTuiPreset preset{};
    std::string error;
    ASSERT_TRUE(parsePresetText(text, preset, error)) << error;

    ASSERT_EQ(preset.jobs.size(), 2u);
    EXPECT_EQ(preset.outputDir.string(), "/mnt/d/cxet-recordings");
    EXPECT_EQ(preset.progressSec, 10);
    EXPECT_EQ(preset.launchWaveSize, 4);
    EXPECT_EQ(preset.launchStaggerMs, 250);
    EXPECT_EQ(preset.sameExchangeCooldownMs, 1500);
    EXPECT_EQ(preset.jobs[0].name, "binance_btc");
    EXPECT_EQ(preset.jobs[0].exchange, "binance");
    EXPECT_EQ(preset.jobs[0].symbol, "BTCUSDT");
    EXPECT_EQ(preset.jobs[0].routeSymbol, "BTC");
    EXPECT_EQ(preset.jobs[0].durationMin, 0);
    EXPECT_TRUE(preset.jobs[0].channels.priceLimit);
    EXPECT_EQ(preset.jobs[1].name, "bybit_eth");
    EXPECT_EQ(preset.jobs[1].durationMin, 30);
    EXPECT_FALSE(preset.jobs[1].channels.funding);
}

TEST(RecorderTuiPreset, DefaultsOutputDirToRecordingsRoot) {
    RecorderTuiPreset preset{};

    EXPECT_EQ(preset.outputDir, hftrec::recordings::defaultRecordingsRoot());
}

TEST(RecorderTuiPreset, KeepsExplicitAbsoluteCDriveOutputDir) {
    constexpr std::string_view text = R"(
output_dir=/mnt/c/Users/be0h/manual-recordings

[job binance_btc]
exchange=binance
market=futures
symbol=BTCUSDT
duration_min=1
channels=bookticker
)";

    RecorderTuiPreset preset{};
    std::string error;
    ASSERT_TRUE(parsePresetText(text, preset, error)) << error;

    EXPECT_EQ(preset.outputDir, std::filesystem::path("/mnt/c/Users/be0h/manual-recordings"));
}

TEST(RecorderTuiPreset, RoundTripsPresetText) {
    RecorderTuiPreset preset{};
    preset.outputDir = "/tmp/recordings";
    preset.progressSec = 5;
    preset.launchWaveSize = 8;
    preset.launchStaggerMs = 100;
    preset.sameExchangeCooldownMs = 750;
    auto& job = preset.jobs.emplace_back();
    job.name = "binance_btc";
    job.exchange = "binance";
    job.market = "futures";
    job.symbol = "BTCUSDT";
    job.routeSymbol = "BTC";
    job.durationMin = 15;
    job.channels.trades = true;
    job.channels.bookTicker = true;
    job.channels.orderbook = true;

    RecorderTuiPreset parsed{};
    std::string error;
    ASSERT_TRUE(parsePresetText(renderPresetText(preset), parsed, error)) << error;

    ASSERT_EQ(parsed.jobs.size(), 1u);
    EXPECT_EQ(parsed.outputDir, preset.outputDir);
    EXPECT_EQ(parsed.progressSec, preset.progressSec);
    EXPECT_EQ(parsed.launchWaveSize, preset.launchWaveSize);
    EXPECT_EQ(parsed.launchStaggerMs, preset.launchStaggerMs);
    EXPECT_EQ(parsed.sameExchangeCooldownMs, preset.sameExchangeCooldownMs);
    EXPECT_EQ(parsed.jobs.front().name, job.name);
    EXPECT_EQ(parsed.jobs.front().exchange, job.exchange);
    EXPECT_EQ(parsed.jobs.front().symbol, job.symbol);
    EXPECT_EQ(parsed.jobs.front().routeSymbol, job.routeSymbol);
    EXPECT_EQ(parsed.jobs.front().durationMin, job.durationMin);
    EXPECT_TRUE(parsed.jobs.front().channels.trades);
    EXPECT_TRUE(parsed.jobs.front().channels.bookTicker);
    EXPECT_TRUE(parsed.jobs.front().channels.orderbook);
}

TEST(RecorderTuiPreset, RejectsJobWithoutSymbol) {
    constexpr std::string_view text = R"(
[job bad]
exchange=binance
market=futures
duration_min=10
channels=trades
)";

    RecorderTuiPreset preset{};
    std::string error;
    EXPECT_FALSE(parsePresetText(text, preset, error));
    EXPECT_NE(error.find("symbol"), std::string::npos);
}

TEST(RecorderTuiPreset, ParsesLaunchOptions) {
    constexpr std::string_view text = R"(
output_dir=./recordings
progress_sec=10
launch_wave_size=6
launch_stagger_ms=300
same_exchange_cooldown_ms=2000

[job binance_btc]
exchange=binance
market=futures
symbol=BTCUSDT
channels=trades
)";

    RecorderTuiPreset preset{};
    std::string error;
    ASSERT_TRUE(parsePresetText(text, preset, error)) << error;

    EXPECT_EQ(preset.launchWaveSize, 6);
    EXPECT_EQ(preset.launchStaggerMs, 300);
    EXPECT_EQ(preset.sameExchangeCooldownMs, 2000);
    EXPECT_EQ(preset.outputDir, hftrec::recordings::defaultRecordingsRoot());
}

TEST(RecorderTuiPreset, ResolvesBarePresetNamesIntoConfigsDirectory) {
    EXPECT_EQ(resolvePresetPath("btw"), std::filesystem::path("configs") / "btw.ini");
    EXPECT_EQ(resolvePresetPath("btw.ini"), std::filesystem::path("configs") / "btw.ini");
    EXPECT_EQ(defaultPresetPath(), std::filesystem::path("configs") / "default.ini");
}

TEST(RecorderTuiPreset, KeepsExplicitPresetPathsUnchanged) {
    EXPECT_EQ(resolvePresetPath("./btw"), std::filesystem::path("./btw"));
    EXPECT_EQ(resolvePresetPath("../btw.ini"), std::filesystem::path("../btw.ini"));
    EXPECT_EQ(resolvePresetPath("/mnt/d/btw.ini"), std::filesystem::path("/mnt/d/btw.ini"));
}

}  // namespace
