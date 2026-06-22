#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>

#include "core/capture/SessionManifest.hpp"
#include "core/recordings/RecordingDiscovery.hpp"

namespace {

std::filesystem::path makeTempRoot() {
    const auto base = std::filesystem::temp_directory_path();
    const auto name = std::string{"hftrec_recording_discovery_"} + std::to_string(std::rand());
    const auto root = base / name;
    std::filesystem::create_directories(root);
    return root;
}

void writeSession(const std::filesystem::path& dir,
                  const std::string& sessionId,
                  const std::string& exchange,
                  const std::string& market,
                  const std::string& symbol,
                  std::int64_t startedAtNs,
                  std::int64_t endedAtNs) {
    std::filesystem::create_directories(dir);
    hftrec::capture::SessionManifest manifest{};
    manifest.sessionId = sessionId;
    manifest.exchange = exchange;
    manifest.market = market;
    manifest.symbols = {symbol};
    manifest.sessionStatus = endedAtNs > 0 ? "complete" : "recording";
    manifest.startedAtNs = startedAtNs;
    manifest.endedAtNs = endedAtNs;
    manifest.bookTickerEnabled = true;
    manifest.bookTickerCount = 123;
    std::ofstream out(dir / "manifest.json", std::ios::out | std::ios::trunc);
    out << hftrec::capture::renderManifestJson(manifest);
}

}  // namespace

TEST(RecordingDiscovery, DiscoversFlatAndGroupedSessions) {
    const auto root = makeTempRoot();
    writeSession(root / "flat_binance", "flat_binance", "binance", "futures", "BTWUSDT", 1782141931000000000LL, 1782141991000000000LL);
    writeSession(root / "2026-06-22_18-25-31_BTWUSDT" / "nested_aster",
                 "nested_aster",
                 "aster",
                 "futures",
                 "BTWUSDT",
                 1782141932000000000LL,
                 1782141992000000000LL);

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 2u);
    ASSERT_EQ(result.groups.size(), 2u);
    EXPECT_TRUE(result.sessions[0].searchText.find("BTWUSDT") != std::string::npos);
    EXPECT_TRUE(result.sessions[0].searchText.find("22.06.2026") != std::string::npos);
}

TEST(RecordingDiscovery, GroupsLegacySessionsWithinFiveMinutesByNormalizedSymbol) {
    const auto root = makeTempRoot();
    writeSession(root / "s1", "s1", "binance", "futures", "BTWUSDT", 1782141931000000000LL, 1782141991000000000LL);
    writeSession(root / "s2", "s2", "kucoin", "futures", "BTWUSDTM", 1782141960000000000LL, 1782141992000000000LL);
    writeSession(root / "s3", "s3", "binance", "futures", "BTWUSDT", 1782142600000000000LL, 1782142660000000000LL);

    const auto plan = hftrec::recordings::organizeRecordings(root, false, 300);

    ASSERT_EQ(plan.moves.size(), 3u);
    EXPECT_EQ(plan.moves[0].groupId, plan.moves[1].groupId);
    EXPECT_NE(plan.moves[0].groupId, plan.moves[2].groupId);
    EXPECT_TRUE(plan.moves[0].groupId.find("BTWUSDT") != std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(plan.moves[0].to));
}

TEST(RecordingDiscovery, SkipsActiveFlatSessionsDuringOrganize) {
    const auto root = makeTempRoot();
    writeSession(root / "active", "active", "binance", "futures", "BTWUSDT", 1782141931000000000LL, 0);

    const auto plan = hftrec::recordings::organizeRecordings(root, false, 300);

    EXPECT_TRUE(plan.moves.empty());
    ASSERT_EQ(plan.skippedActive.size(), 1u);
    EXPECT_EQ(plan.skippedActive.front(), "active");
}
