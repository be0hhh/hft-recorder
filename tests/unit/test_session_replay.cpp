#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "CapturedArrivalTestData.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
using hftrec::capture::SessionManifest;
using hftrec::replay::SessionReplay;
namespace captured = hftrec::test_support;

fs::path makeTmpDir() {
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = fs::temp_directory_path() /
        ("hftrec_session_replay_" + std::to_string(stamp) + "_" +
         std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + "_" +
         std::to_string(std::rand()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "jsonl", ec);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

void writeManifest(const fs::path& dir,
                   const captured::ChannelCounts& counts,
                   std::string exchange = "binance") {
    writeFile(dir / "manifest.json",
              hftrec::capture::renderManifestJson(
                  captured::manifest(counts, std::move(exchange))));
}

TEST(SessionManifest, OnlyCurrentCapturedArrivalContractIsReadable) {
    SessionManifest manifest = captured::manifest({.trades = 1u});
    manifest.tradesRuntime.firstRowNs = 100;
    manifest.tradesRuntime.lastRowNs = 200;

    const std::string document = hftrec::capture::renderManifestJson(manifest);
    EXPECT_NE(document.find("\"manifest_schema_version\": 3"), std::string::npos);
    EXPECT_NE(document.find("\"arrival_clock\""), std::string::npos);
    EXPECT_NE(document.find("\"runtime_health\""), std::string::npos);
    EXPECT_TRUE(hftrec::capture::isSupportedManifestSchemaVersion(3));
    EXPECT_FALSE(hftrec::capture::isSupportedManifestSchemaVersion(1));
    EXPECT_FALSE(hftrec::capture::isSupportedManifestSchemaVersion(2));

    SessionManifest parsed{};
    ASSERT_EQ(hftrec::capture::parseManifestJson(document, parsed), Status::Ok);
    EXPECT_EQ(parsed.manifestSchemaVersion, 3);
    EXPECT_EQ(parsed.captureContractVersion,
              hftrec::capture::kCaptureContractVersionCurrent);
    EXPECT_EQ(parsed.arrivalClock.capturedRows, 1u);

    constexpr std::string_view oldManifest =
        R"({"manifest_schema_version":2,"corpus_schema_version":2})";
    EXPECT_EQ(hftrec::capture::parseManifestJson(oldManifest, parsed),
              Status::CorruptData);

    constexpr std::string_view implicitStatus = R"({
      "manifest_schema_version":3,
      "corpus_schema_version":3,
      "capture_contract_version":"hftrec.captured_arrival_rows_json.v4",
      "arrival_clock":{
        "boundary":"hft-parser.application-frame-ready",
        "realtime_clock":"CLOCK_REALTIME",
        "monotonic_clock":"CLOCK_MONOTONIC"
      }
    })";
    EXPECT_EQ(hftrec::capture::parseManifestJson(implicitStatus, parsed),
              Status::CorruptData);
}

TEST(SessionReplay, EndToEndUsesCurrentDepthPairAndExchangeAxisForViewer) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 2u, .depth = 2u});
    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              captured::depthTapeRow(2000, 1, {{30000, 7, 0}, {30100, 4, 1}}) +
              captured::depthTapeRow(3500, 3, {{30100, 0, 1}, {30200, 8, 1}}));
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              captured::depthSidecarRow(2000, 1, {{30000, 7, 0}, {30100, 4, 1}}) +
              captured::depthSidecarRow(3500, 3, {{30100, 0, 1}, {30200, 8, 1}}));
    writeFile(dir / "jsonl" / "trades.jsonl",
              captured::tradeRow(30050, 1, 1, 2500, 2) +
              captured::tradeRow(30200, 2, 0, 4000, 4));

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_FALSE(replay.gapDetected());
    EXPECT_EQ(replay.trades().size(), 2u);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.events().size(), 4u);
    EXPECT_EQ(replay.firstTsNs(), 2000);
    EXPECT_EQ(replay.lastTsNs(), 4000);

    replay.seek(2000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    replay.seek(5000);
    EXPECT_EQ(replay.book().asks().count(30100), 0u);
    EXPECT_EQ(replay.book().bestAskPrice(), 30200);
    replay.seek(1000);
    EXPECT_EQ(replay.book().bestBidPrice(), 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MissingDirectoryReturnsError) {
    SessionReplay replay{};
    EXPECT_EQ(replay.open("/this/path/does/not/exist/for/sure_xyz"),
              Status::InvalidArgument);
    EXPECT_NE(std::string{replay.errorDetail()}.find("session directory does not exist"),
              std::string::npos);
}

TEST(SessionReplay, MissingManifestIsRejected) {
    const auto dir = makeTmpDir();
    writeFile(dir / "jsonl" / "trades.jsonl",
              captured::tradeRow(30050, 1, 1, 2500, 1));
    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("manifest.json"),
              std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, InvalidCapturedRowReportsFileAndLine) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 2u});
    writeFile(dir / "jsonl" / "trades.jsonl",
              captured::tradeRow(30050, 1, 1, 2500, 1) +
              "[30051,1,\"bad\",2600]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"),
              std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state,
              hftrec::ChannelHealthState::Corrupt);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, RejectsRowsWithoutCapturedArrivalTail) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 1u});
    writeFile(dir / "jsonl" / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTC_USDT\",\"binance\",\"futures_usd\",1,1]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_TRUE(replay.trades().empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, PartialCurrentDepthPairKeepsOnlyValidPrefixForViewer) {
    const auto dir = makeTmpDir();
    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              captured::depthTapeRow(100, 1, {{30000, 10, 0}, {30001, 20, 1}}) +
              captured::depthTapeRow(200, 2, {{30002, 30, 0}}) +
              captured::depthTapeRow(300, 3, {{30003, 40, 0}}));
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              captured::depthSidecarRow(100, 1, {{30000, 10, 0}, {30001, 20, 1}}) +
              captured::depthSidecarRow(200, 2, {{30002, 30, 0}}));

    SessionReplay replay{};
    EXPECT_EQ(replay.addDepthFileAllowPartial(
                  dir / "jsonl" / "depth_tape.jsonl"),
              Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("line count mismatch at line 3"),
              std::string::npos);
    EXPECT_EQ(replay.depths().size(), 2u);
    replay.finalize();
    EXPECT_EQ(replay.status(), Status::Ok);
    EXPECT_EQ(replay.integritySummary().depth.state,
              hftrec::ChannelHealthState::Corrupt);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, LegacyDepthJsonFileIsNotAccepted) {
    const auto dir = makeTmpDir();
    writeFile(dir / "depth.jsonl", "[[30000,7,0],2000]\n");
    SessionReplay replay{};
    EXPECT_EQ(replay.addDepthFile(dir / "depth.jsonl"),
              Status::InvalidArgument);
    EXPECT_NE(std::string{replay.errorDetail()}.find("depth_tape.jsonl"),
              std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, KeepsHistoricalCandlesOutsideLiveArrivalClass) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.candles = 1u, .candles2 = 1u});
    writeFile(dir / "jsonl" / "candles.jsonl",
              captured::candleRow(1, 1000, 10000000000LL, 1));
    writeFile(dir / "jsonl" / "candles2.jsonl",
              captured::candleRow(1, 2000, 20000000000LL, 2));

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.candles().size(), 1u);
    ASSERT_EQ(replay.candles2().size(), 1u);
    EXPECT_TRUE(hftrec::replay::isHistoricalBackfill(
        replay.candles().front().arrival));
    EXPECT_EQ(replay.firstTsNs(), 1000);
    EXPECT_EQ(replay.lastTsNs(), 2000);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, SameExchangeTimestampRowsShareViewerBucket) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 1u, .bookTickers = 1u, .depth = 1u});
    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              captured::depthTapeRow(2000, 1, {{30000, 7, 0}, {30100, 4, 1}}));
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              captured::depthSidecarRow(2000, 1, {{30000, 7, 0}, {30100, 4, 1}}));
    writeFile(dir / "jsonl" / "trades.jsonl",
              captured::tradeRow(30050, 1, 1, 2000, 2));
    writeFile(dir / "jsonl" / "bookticker.jsonl",
              captured::bookTickerRow(30000, 7, 30100, 4, 2000, 3));

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.events().size(), 3u);
    ASSERT_EQ(replay.buckets().size(), 1u);
    EXPECT_EQ(replay.buckets().front().items.size(), 3u);
    replay.seek(2000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, LoadsCurrentReferenceChannels) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.markPrices = 1u, .indexPrices = 1u,
                        .fundings = 1u, .priceLimits = 1u});
    writeFile(dir / "jsonl" / "mark_price.jsonl",
              "[2000,30000,1,1," + captured::arrivalTail(1, 2000) + "]\n");
    writeFile(dir / "jsonl" / "index_price.jsonl",
              "[2100,29990,2,2," + captured::arrivalTail(2, 2100) + "]\n");
    writeFile(dir / "jsonl" / "funding.jsonl",
              "[2200,125,2000,2400,3,3," + captured::arrivalTail(3, 2200) + "]\n");
    writeFile(dir / "jsonl" / "price_limit.jsonl",
              "[2300,31000,29000,1,4,4," + captured::arrivalTail(4, 2300) + "]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_EQ(replay.markPrices().front().markPriceE8, 30000);
    EXPECT_EQ(replay.indexPrices().front().indexPriceE8, 29990);
    EXPECT_EQ(replay.fundings().front().fundingRateE8, 125);
    EXPECT_EQ(replay.priceLimits().front().buyLimitE8, 31000);
    EXPECT_EQ(replay.events().size(), 4u);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, ExchangeTimestampOrderMayDifferFromCapturedArrivalOrder) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 1u, .bookTickers = 1u, .depth = 1u});
    writeFile(dir / "jsonl" / "trades.jsonl",
              captured::tradeRow(30050, 1, 1, 2000, 1));
    writeFile(dir / "jsonl" / "bookticker.jsonl",
              captured::bookTickerRow(30000, 7, 30100, 4, 1500, 2));
    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              captured::depthTapeRow(3000, 3, {{30000, 7, 0}, {30100, 4, 1}}));
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              captured::depthSidecarRow(3000, 3, {{30000, 7, 0}, {30100, 4, 1}}));

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_EQ(replay.integritySummary().sessionHealth,
              hftrec::SessionHealth::Clean);
    EXPECT_EQ(replay.bookTickers().front().arrival.shardSequence, 2u);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MissingManifestDeclaredArtifactFailsClosed) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.trades = 1u});
    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_EQ(replay.integritySummary().sessionHealth,
              hftrec::SessionHealth::Corrupt);
    EXPECT_TRUE(fs::exists(dir / "reports" / "integrity_report.json"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, NormalizesBitgetFixedDepthSnapshotsOnCurrentTape) {
    const auto dir = makeTmpDir();
    writeManifest(dir, {.depth = 2u}, "bitget");
    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              captured::depthTapeRow(2000, 1, {{100, 5, 0}, {101, 4, 0}, {110, 3, 1}}) +
              captured::depthTapeRow(3000, 2, {{101, 7, 0}, {111, 2, 1}}));
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              captured::depthSidecarRow(2000, 1, {{100, 5, 0}, {101, 4, 0}, {110, 3, 1}}) +
              captured::depthSidecarRow(3000, 2, {{101, 7, 0}, {111, 2, 1}}));

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.depths().size(), 2u);
    const auto& second = replay.depths()[1].levels;
    const auto hasDelete = [&](std::int64_t price, std::int64_t side) {
        return std::find_if(second.begin(), second.end(), [&](const auto& level) {
            return level.priceE8 == price && level.qtyE8 == 0 &&
                level.side == side;
        }) != second.end();
    };
    EXPECT_TRUE(hasDelete(100, 0));
    EXPECT_TRUE(hasDelete(110, 1));
    replay.seek(3000);
    EXPECT_EQ(replay.book().bestBidPrice(), 101);
    EXPECT_EQ(replay.book().bestAskPrice(), 111);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
