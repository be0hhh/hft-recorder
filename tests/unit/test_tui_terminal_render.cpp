#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"
#include "core/tui/TerminalRender.hpp"

namespace {

using hftrec::tui::ChannelSelection;
using hftrec::tui::RecorderTuiJob;
using hftrec::tui::RecorderTuiPreset;
using hftrec::tui::RunLoadClass;
using hftrec::tui::TerminalViewport;
using hftrec::tui::classifyRunLoad;
using hftrec::tui::compactSessionPath;
using hftrec::tui::limitLinesForViewport;
using hftrec::tui::requiresHeavyRunConfirmation;
using hftrec::tui::truncateForTerminal;

TEST(TuiTerminalRender, TruncatesLongLinesToViewportWidth) {
    const std::string line = truncateForTerminal("abcdefghijklmnopqrstuvwxyz", 10);

    EXPECT_EQ(line.size(), 10u);
    EXPECT_EQ(line, "abcdefg...");
}

TEST(TuiTerminalRender, KeepsShortLinesUnchanged) {
    EXPECT_EQ(truncateForTerminal("BTCUSDT", 20), "BTCUSDT");
}

TEST(TuiTerminalRender, CompactsSessionPathToFitWidth) {
    const std::filesystem::path path =
        "/mnt/d/recordings/2026-06-22_23-54-03_LABUSDT";

    const std::string compact = compactSessionPath(path, 32);

    EXPECT_LE(compact.size(), 32u);
    EXPECT_NE(compact.find("..."), std::string::npos);
    EXPECT_NE(compact.find("2026-06-22_23-54-03_LABUSDT"), std::string::npos);
}

TEST(TuiTerminalRender, LimitsLinesToViewportAndReportsHiddenCount) {
    const std::vector<std::string> lines = {"one", "two", "three", "four", "five"};

    const std::vector<std::string> limited = limitLinesForViewport(lines, TerminalViewport{.rows = 5, .cols = 80}, 2);

    ASSERT_EQ(limited.size(), 3u);
    EXPECT_EQ(limited[0], "one");
    EXPECT_EQ(limited[1], "two");
    EXPECT_EQ(limited[2], "... 3 more lines hidden");
}

TEST(TuiTerminalRender, ClassifiesMultiJobIndefiniteOrderbookPresetAsHeavy) {
    RecorderTuiPreset preset{};
    preset.jobs.clear();

    RecorderTuiJob first{};
    first.name = "binance";
    first.durationMin = 0;
    first.channels.orderbook = true;
    preset.jobs.push_back(first);

    RecorderTuiJob second = first;
    second.name = "bitget";
    second.channels.bookTicker = true;
    preset.jobs.push_back(second);

    EXPECT_EQ(classifyRunLoad(preset), RunLoadClass::Heavy);
    EXPECT_TRUE(requiresHeavyRunConfirmation(preset));
}

TEST(TuiTerminalRender, DoesNotRequireConfirmationForSingleBoundedJob) {
    RecorderTuiPreset preset{};
    preset.jobs.clear();

    RecorderTuiJob job{};
    job.name = "binance";
    job.durationMin = 15;
    job.channels.orderbook = true;
    preset.jobs.push_back(job);

    EXPECT_EQ(classifyRunLoad(preset), RunLoadClass::Normal);
    EXPECT_FALSE(requiresHeavyRunConfirmation(preset));
}

TEST(TuiTerminalRender, TreatsAllChannelsAsHighRate) {
    RecorderTuiPreset preset{};
    preset.jobs.clear();

    RecorderTuiJob first{};
    first.name = "one";
    first.durationMin = 0;
    first.channels = hftrec::tui::allLiveChannels();
    preset.jobs.push_back(first);

    RecorderTuiJob second = first;
    second.name = "two";
    preset.jobs.push_back(second);

    EXPECT_TRUE(requiresHeavyRunConfirmation(preset));
}

}  // namespace
