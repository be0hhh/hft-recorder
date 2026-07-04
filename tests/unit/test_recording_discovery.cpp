#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    manifest.tradesEnabled = true;
    manifest.tradesCount = 45;
    manifest.bookTickerEnabled = true;
    manifest.bookTickerCount = 123;
    manifest.sessionHealth = sessionHealth;
    manifest.warningSummary = warningSummary;
    std::ofstream out(dir / "manifest.json", std::ios::out | std::ios::trunc);
    out << hftrec::capture::renderManifestJson(manifest);
}

void writeEmptySession(const std::filesystem::path& dir,
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
    manifest.sessionStatus = "failed_empty";
    manifest.startedAtNs = startedAtNs;
    manifest.endedAtNs = endedAtNs;
    manifest.tradesEnabled = true;
    manifest.bookTickerEnabled = true;
    std::ofstream out(dir / "manifest.json", std::ios::out | std::ios::trunc);
    out << hftrec::capture::renderManifestJson(manifest);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out << text;
}

void injectLegacyRouteSymbols(const std::filesystem::path& sessionPath,
                              const std::string& symbol,
                              const std::string& routeSymbol) {
    const std::filesystem::path manifestPath = sessionPath / "manifest.json";
    std::string text = readText(manifestPath);
    const std::string needle = "    \"symbols\": [\"" + symbol + "\"],\n";
    const std::string replacement = needle + "    \"route_symbols\": [\"" + routeSymbol + "\"],\n";
    const auto pos = text.find(needle);
    if (pos == std::string::npos) return;
    text.replace(pos, needle.size(), replacement);
    writeText(manifestPath, text);
}

}  // namespace

TEST(RecordingDiscovery, NormalizesDerivativeSymbolVariantsForStorage) {
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDTM"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW-USDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW_USDT"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW-USDT-SWAP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDTSWAP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTW_USDT_PERP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTWUSDTPERP"), "BTWUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTC-USD-SWAP"), "BTCUSD");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("BTC-USD-PERP"), "BTCUSD");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("sSYNUSDT"), "SYNUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("slx_usdt"), "SLXUSDT");
    EXPECT_EQ(hftrec::recordings::normalizeRecordingSymbol("synusdt"), "SYNUSDT");
}

TEST(RecordingDiscovery, BuildsLocalAndFolderSymbolsForStorage) {
    EXPECT_TRUE(hftrec::recordings::recordingLocalSymbol("BTWUSDT").empty());
    EXPECT_TRUE(hftrec::recordings::recordingLocalSymbol("BTW-USDT-SWAP").empty());
    EXPECT_TRUE(hftrec::recordings::recordingLocalSymbol("BTW_USDT_PERP").empty());
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("1000_PEPE_USDT"), "1000_PEPE_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("龙虾:USDT"), "龙虾_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("BTW_USDT"), "BTW_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("1000_PEPE_USDT"), "1000_PEPE_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("龙虾_USDT"), "%E9%BE%99%E8%99%BE_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("币安人生_USDT"), "%E5%B8%81%E5%AE%89%E4%BA%BA%E7%94%9F_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("我踏马来了_USDT"), "%E6%88%91%E8%B8%8F%E9%A9%AC%E6%9D%A5%E4%BA%86_USDT");
    EXPECT_NE(hftrec::recordings::recordingFolderSymbol("龙虾_USDT"),
              hftrec::recordings::recordingFolderSymbol("币安人生_USDT"));
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("1000%3APEPE%3AUSDT"), "1000_PEPE_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("%E9%BE%99%E8%99%BE%3AUSDT"), "龙虾_USDT");
    EXPECT_EQ(hftrec::recordings::recordingSessionFolderName(1782141931000000000LL,
                                                             "okx",
                                                             "futures",
                                                             "BTW_USDT"),
              "1782141931000000000_okx_futures_BTW_USDT");
    EXPECT_EQ(hftrec::recordings::recordingSessionFolderName(1782141931000000000LL,
                                                             "binance",
                                                             "futures",
                                                             "龙虾_USDT"),
              "1782141931000000000_binance_futures_%E9%BE%99%E8%99%BE_USDT");
    const std::string utf8GroupName = hftrec::recordings::recordingGroupFolderName(
        1782141931000000000LL,
        hftrec::recordings::recordingFolderSymbol("龙虾_USDT"));
    EXPECT_NE(utf8GroupName.find("%E9%BE%99%E8%99%BE_USDT"), std::string::npos);
    EXPECT_EQ(utf8GroupName.find(':'), std::string::npos);
}

TEST(RecordingDiscovery, BuildsLocalSymbolsFromExchangeNativeFormats) {
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("okx", "futures", "btc-usdt-swap"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("okx", "spot", "btc-usdt"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("kucoin", "futures", "XBTUSDTM"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("kucoin", "spot", "btc-usdt"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("gate", "futures", "btc_usdt"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("toobit", "futures", "btc-swap-usdt"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("phemex", "spot", "sBTCUSDT"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("xt", "spot", "btc_usdt"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("hyperliquid", "futures", "BTC"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("hyperliquid", "futures", "BTCUSDT"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingLocalSymbol("binance", "futures", "BTCUSDT"), "BTC_USDT");
    EXPECT_EQ(hftrec::recordings::recordingFolderSymbol("okx", "futures", "btc-usdt-swap"), "BTC_USDT");
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
    writeSession(root / "flat_binance", "flat_binance", "binance", "futures", "BTW_USDT", 1782141931000000000LL, 1782141991000000000LL);
    writeSession(root / "2026-06-22_18-25-31_BTW%3AUSDT" / "nested_aster",
                 "nested_aster",
                 "aster",
                 "futures",
                 "BTW_USDT",
                 1782141932000000000LL,
                 1782141992000000000LL);

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 2u);
    ASSERT_EQ(result.groups.size(), 2u);
    EXPECT_TRUE(result.sessions[0].searchText.find("BTW_USDT") != std::string::npos);
    EXPECT_TRUE(result.sessions[0].searchText.find("22.06.2026") != std::string::npos);
    EXPECT_EQ(result.sessions[0].tradesCount, 45u);
    EXPECT_EQ(result.sessions[0].bookTickerCount, 123u);
}

TEST(RecordingDiscovery, UsesProgressiveTempManifestWhenPrimaryIsEmpty) {
    const auto root = makeTempRoot();
    const auto sessionDir = root / "1782141931000000000_binance_futures_BTW_USDT";
    writeSession(sessionDir,
                 "1782141931000000000_binance_futures_BTW_USDT",
                 "binance",
                 "futures",
                 "BTW_USDT",
                 1782141931000000000LL,
                 1782141991000000000LL);
    std::filesystem::rename(sessionDir / "manifest.json", sessionDir / "manifest.json.tmp");
    std::ofstream empty(sessionDir / "manifest.json", std::ios::out | std::ios::trunc);
    empty.close();

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 1u);
    EXPECT_EQ(result.sessions.front().sessionId, "1782141931000000000_binance_futures_BTW_USDT");
    EXPECT_EQ(result.sessions.front().manifestPath.filename().string(), "manifest.json.tmp");
}

TEST(RecordingDiscovery, UsesPreviousManifestWhenPrimaryIsMissing) {
    const auto root = makeTempRoot();
    const auto sessionDir = root / "1782141931000000000_binance_futures_BTW_USDT";
    writeSession(sessionDir,
                 "1782141931000000000_binance_futures_BTW_USDT",
                 "binance",
                 "futures",
                 "BTW_USDT",
                 1782141931000000000LL,
                 1782141991000000000LL);
    std::filesystem::rename(sessionDir / "manifest.json", sessionDir / "manifest.json.prev");

    const auto result = hftrec::recordings::discoverRecordings(root);

    ASSERT_EQ(result.sessions.size(), 1u);
    EXPECT_EQ(result.sessions.front().sessionId, "1782141931000000000_binance_futures_BTW_USDT");
    EXPECT_EQ(result.sessions.front().manifestPath.filename().string(), "manifest.json.prev");
}

TEST(RecordingDiscovery, ExposesSessionHealthAndCaptureWarning) {
    const auto root = makeTempRoot();
    writeSession(root / "degraded_bitget",
                 "degraded_bitget",
                 "bitget",
                 "futures",
                 "AGLD_USDT",
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

TEST(RecordingDiscovery, WritesGroupManifestForTargetPathWithoutScanningSiblingGroups) {
    const auto root = makeTempRoot();
    const auto targetGroup = root / "2026-07-03_09-04-06_TLM_USDT";
    writeSession(targetGroup / "binance",
                 "binance",
                 "binance",
                 "futures",
                 "TLM_USDT",
                 1783058649564013789LL,
                 1783058759873849337LL);
    writeSession(root / "other_group" / "bybit",
                 "bybit",
                 "bybit",
                 "futures",
                 "BTC_USDT",
                 1783058649564013789LL,
                 1783058759873849337LL);

    std::string error;
    ASSERT_TRUE(hftrec::recordings::writeGroupManifestForPath(root, targetGroup, &error)) << error;

    const auto targetManifest = targetGroup / "group_manifest.json";
    ASSERT_TRUE(std::filesystem::exists(targetManifest));
    std::ifstream in(targetManifest);
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(json.find("\"session_id\": \"binance\""), std::string::npos);
    EXPECT_EQ(json.find("\"session_id\": \"bybit\""), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "other_group" / "group_manifest.json"));
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
    EXPECT_TRUE(plan.moves[0].groupId.find("BTW_USDT") != std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(plan.moves[0].to));
}

TEST(RecordingDiscovery, OrganizeApplyRewritesLegacyNativeManifestToLocalLayout) {
    const auto root = makeTempRoot();
    const auto oldGroup = root / "2026-06-22_18-25-31_BTWUSDT";
    const auto oldSession = oldGroup / "1782141931_okx_futures_BTW-USDT-SWAP";
    writeSession(oldSession,
                 "1782141931_okx_futures_BTW-USDT-SWAP",
                 "okx",
                 "futures",
                 "BTW-USDT-SWAP",
                 1782141931000000000LL,
                 1782141991000000000LL);
    const auto oldBacktestManifest = oldSession / "backtests" / "stat_arb_edge_maker-BTW-USDT-SWAP" / "manifest.json";
    std::filesystem::create_directories(oldBacktestManifest.parent_path());
    writeText(oldBacktestManifest,
              "{\n"
              "  \"legs\": [{\"session_path\": \"" + oldSession.string() +
              "\", \"exchange\": \"okx\", \"market\": \"futures\", \"symbol\": \"BTW-USDT-SWAP\"}]\n"
              "}\n");

    const auto plan = hftrec::recordings::organizeRecordings(root, true, 300);

    if (!plan.errors.empty()) ADD_FAILURE() << plan.errors.front();
    ASSERT_TRUE(plan.errors.empty());
    ASSERT_EQ(plan.moves.size(), 1u);
    EXPECT_TRUE(plan.moves.front().pathChanged);
    EXPECT_TRUE(plan.moves.front().manifestUpdated);
    EXPECT_EQ(plan.moves.front().sessionId, "1782141931_okx_futures_BTW_USDT");
    EXPECT_EQ(plan.moves.front().to.filename().string(), "1782141931_okx_futures_BTW_USDT");
    EXPECT_TRUE(plan.moves.front().to.parent_path().filename().string().find("BTW_USDT") != std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(oldSession));
    EXPECT_FALSE(std::filesystem::exists(oldGroup));

    hftrec::capture::SessionManifest manifest{};
    ASSERT_TRUE(hftrec::isOk(hftrec::capture::parseManifestJson(readText(plan.moves.front().to / "manifest.json"), manifest)));
    ASSERT_EQ(manifest.symbols.size(), 1u);
    EXPECT_EQ(manifest.symbols.front(), "BTW_USDT");
    EXPECT_EQ(manifest.storageSymbol, "BTW_USDT");
    EXPECT_EQ(manifest.sessionId, plan.moves.front().to.filename().string());
    EXPECT_EQ(readText(plan.moves.front().to / "manifest.json").find("route_symbols"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(plan.moves.front().to.parent_path() / "group_manifest.json"));
    const auto newBacktestManifest = plan.moves.front().to / "backtests" / "stat_arb_edge_maker-BTW_USDT" / "manifest.json";
    ASSERT_TRUE(std::filesystem::exists(newBacktestManifest));
    const std::string backtestText = readText(newBacktestManifest);
    EXPECT_NE(backtestText.find(plan.moves.front().to.string()), std::string::npos);
    EXPECT_NE(backtestText.find("\"symbol\": \"BTW_USDT\""), std::string::npos);
    EXPECT_EQ(backtestText.find(oldSession.string()), std::string::npos);
    EXPECT_EQ(backtestText.find("\"BTW-USDT-SWAP\""), std::string::npos);
}

TEST(RecordingDiscovery, OrganizeApplyRemovesLegacyRouteSymbolsFromLocalManifest) {
    const auto root = makeTempRoot();
    const std::int64_t startNs = 1782141931000000000LL;
    const auto group = root / hftrec::recordings::recordingGroupFolderName(startNs, "BTW_USDT");
    const auto session = group / "1782141931_okx_futures_BTW_USDT";
    writeSession(session,
                 "1782141931_okx_futures_BTW_USDT",
                 "okx",
                 "futures",
                 "BTW_USDT",
                 startNs,
                 1782141991000000000LL);
    injectLegacyRouteSymbols(session, "BTW_USDT", "BTW-USDT-SWAP");

    const auto plan = hftrec::recordings::organizeRecordings(root, true, 300);

    if (!plan.errors.empty()) ADD_FAILURE() << plan.errors.front();
    ASSERT_TRUE(plan.errors.empty());
    ASSERT_EQ(plan.moves.size(), 1u);
    EXPECT_FALSE(plan.moves.front().pathChanged);
    EXPECT_TRUE(plan.moves.front().manifestUpdated);
    EXPECT_EQ(readText(session / "manifest.json").find("route_symbols"), std::string::npos);
}

TEST(RecordingDiscovery, SkipsActiveFlatSessionsDuringOrganize) {
    const auto root = makeTempRoot();
    writeSession(root / "active", "active", "binance", "futures", "BTW_USDT", 1782141931000000000LL, 0);

    const auto plan = hftrec::recordings::organizeRecordings(root, false, 300);

    EXPECT_TRUE(plan.moves.empty());
    ASSERT_EQ(plan.skippedActive.size(), 1u);
    EXPECT_EQ(plan.skippedActive.front(), "active");
}

TEST(RecordingDiscovery, OrganizeApplyQuarantinesInactiveEmptySessionsWhenRequested) {
    const auto root = makeTempRoot();
    const auto emptySession = root / "1782141931_binance_futures_BTC_USDT";
    writeEmptySession(emptySession,
                      "1782141931_binance_futures_BTC_USDT",
                      "binance",
                      "futures",
                      "BTC_USDT",
                      1782141931000000000LL,
                      1782141991000000000LL);

    const auto plan = hftrec::recordings::organizeRecordings(root, true, 300, true);

    if (!plan.errors.empty()) ADD_FAILURE() << plan.errors.front();
    ASSERT_TRUE(plan.errors.empty());
    ASSERT_TRUE(plan.moves.empty());
    ASSERT_EQ(plan.quarantinedEmpty.size(), 1u);
    EXPECT_FALSE(std::filesystem::exists(emptySession));
    EXPECT_TRUE(std::filesystem::exists(plan.quarantinedEmpty.front().to / "manifest.json"));
    EXPECT_NE(plan.quarantinedEmpty.front().to.string().find(".deleted_empty"), std::string::npos);
}
