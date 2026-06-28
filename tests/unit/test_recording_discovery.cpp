#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>

#include "core/capture/SessionManifest.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"

namespace {

std::filesystem::path makeTempRoot() {
    static std::atomic<std::uint64_t> counter{0};
    const auto base = std::filesystem::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = std::string{"hftrec_recording_discovery_"} +
                      std::to_string(stamp) + "_" +
                      std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + "_" +
                      std::to_string(std::rand());
    const auto root = base / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
}

void writeSession(const std::filesystem::path& dir,
                  const std::string& sessionId,
                  const std::string& exchange,
                  const std::string& market,
                  const std::string& symbol,
                  std::int64_t startedAtNs,
                  std::int64_t endedAtNs,
                  hftrec::SessionHealth sessionHealth = hftrec::SessionHealth::Clean,
                  const std::string& warningSummary = {}) {
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
    manifest.sessionHealth = sessionHealth;
    manifest.warningSummary = warningSummary;
    std::ofstream out(dir / "manifest.json", std::ios::out | std::ios::trunc);
    out << hftrec::capture::renderManifestJson(manifest);
}

}  // namespace

TEST(RecordingDiscovery, NormalizesDerivativeSymbolVariantsForStorage) {
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDTM"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW-USDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW_USDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW-USDT-SWAP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDTSWAP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTC-USD-SWAP"), "BTCUSD");
}

TEST(RecordingRoot, DefaultsAndRedirectsLegacyRecordingRootsToDDrive) {
    const auto root = hftrec::recordings::defaultRecordingsRoot();

    EXPECT_EQ(root, std::filesystem::path("/mnt/d/recordings"));
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("./recordings"), root);
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("./recordings/group_a"), root / "group_a");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("apps/hft-recorder/recordings/group_a"), root / "group_a");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("/mnt/c/Users/be0h/PycharmProjects/CXETCPP/apps/hft-recorder/recordings/group_a"), root / "group_a");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("d:recordings"), root);
    EXPECT_EQ(hftrec::recordings::normalizeRecordingsPath("D:\\recordings\\group_a"), root / "group_a");
}

TEST(RecordingRoot, ExplicitRecordingRootsAllowAbsoluteCDrivePaths) {
    const auto root = hftrec::recordings::defaultRecordingsRoot();
    const auto explicitC = std::filesystem::path("/mnt/c/Users/be0h/PycharmProjects/CXETCPP/apps/hft-recorder/recordings/group_a");

    EXPECT_EQ(hftrec::recordings::normalizeExplicitRecordingsPath(explicitC), explicitC);
    EXPECT_EQ(hftrec::recordings::normalizeExplicitRecordingsPath("C:\\Users\\be0h\\captures"), std::filesystem::path("/mnt/c/Users/be0h/captures"));
    EXPECT_EQ(hftrec::recordings::normalizeExplicitRecordingsPath("./recordings"), root);
    EXPECT_EQ(hftrec::recordings::normalizeExplicitRecordingsPath("apps/hft-recorder/recordings/group_a"), root / "group_a");
    EXPECT_EQ(hftrec::recordings::normalizeExplicitRecordingsPath("D:\\recordings\\group_a"), root / "group_a");
}

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

TEST(RecordingDiscovery, UsesProgressiveTempManifestWhenPrimaryIsEmpty) {
    const auto root = makeTempRoot();
    const auto sessionDir = root / "1782141931000000000_binance_futures_BTWUSDT";
    writeSession(sessionDir,
                 "1782141931000000000_binance_futures_BTWUSDT",
                 "binance",
                 "futures",
                 "BTWUSDT",
                 1782141931000000000LL,
                 1782141991000000000LL);
    std::filesystem::rename(sessionDir / "manifest.json", sessionDir / "manifest.json.tmp");
    std::ofstream empty(sessionDir / "manifest.json", std::ios::out | std::ios::trunc);
    empty.close();

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 1u);
    EXPECT_EQ(result.sessions.front().sessionId, "1782141931000000000_binance_futures_BTWUSDT");
    EXPECT_EQ(result.sessions.front().manifestPath.filename().string(), "manifest.json.tmp");
}

TEST(RecordingDiscovery, UsesPreviousManifestWhenPrimaryIsMissing) {
    const auto root = makeTempRoot();
    const auto sessionDir = root / "1782141931000000000_binance_futures_BTWUSDT";
    writeSession(sessionDir,
                 "1782141931000000000_binance_futures_BTWUSDT",
                 "binance",
                 "futures",
                 "BTWUSDT",
                 1782141931000000000LL,
                 1782141991000000000LL);
    std::filesystem::rename(sessionDir / "manifest.json", sessionDir / "manifest.json.prev");

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 1u);
    EXPECT_EQ(result.sessions.front().sessionId, "1782141931000000000_binance_futures_BTWUSDT");
    EXPECT_EQ(result.sessions.front().manifestPath.filename().string(), "manifest.json.prev");
}

TEST(RecordingDiscovery, ExposesSessionHealthAndCaptureWarning) {
    const auto root = makeTempRoot();
    writeSession(root / "degraded_bitget",
                 "degraded_bitget",
                 "bitget",
                 "futures",
                 "AGLDUSDT",
                 1782141931000000000LL,
                 1782141991000000000LL,
                 hftrec::SessionHealth::Degraded,
                 "reference: route status=disconnected stream=mark_price symbol=AGLDUSDT");

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 1u);
    EXPECT_EQ(result.sessions.front().sessionHealth, "degraded");
    EXPECT_EQ(result.sessions.front().warningSummary,
              "reference: route status=disconnected stream=mark_price symbol=AGLDUSDT");
}

TEST(RecordingDiscovery, GroupsLegacySessionsWithinFiveMinutesByNormalizedSymbol) {
    const auto root = makeTempRoot();
    writeSession(root / "s1", "s1", "binance", "futures", "BTWUSDT", 1782141931000000000LL, 1782141991000000000LL);
    writeSession(root / "s2_okx", "s2_okx", "okx", "futures", "BTW-USDT-SWAP", 1782141940000000000LL, 1782141992000000000LL);
    writeSession(root / "s3_kucoin", "s3_kucoin", "kucoin", "futures", "BTWUSDTM", 1782141960000000000LL, 1782141992000000000LL);
    writeSession(root / "s4", "s4", "binance", "futures", "BTWUSDT", 1782142600000000000LL, 1782142660000000000LL);

    const auto plan = hftrec::recordings::organizeRecordings(root, false, 300);

    ASSERT_EQ(plan.moves.size(), 4u);
    EXPECT_EQ(plan.moves[0].groupId, plan.moves[1].groupId);
    EXPECT_EQ(plan.moves[0].groupId, plan.moves[2].groupId);
    EXPECT_NE(plan.moves[0].groupId, plan.moves[3].groupId);
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
