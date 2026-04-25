#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/capture/SessionManifest.hpp"
#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
using hftrec::capture::SessionManifest;
using hftrec::capture::renderManifestJson;
using hftrec::replay::SessionReplay;

fs::path makeTmpDir() {
    const auto base = fs::temp_directory_path();
    auto dir = base / ("hftrec_session_replay_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string makeSnapshotJson(std::int64_t tsNs,
                             std::int64_t captureSeq,
                             std::int64_t ingestSeq,
                             std::int64_t updateId,
                             std::int64_t firstUpdateId) {
    return "[[[30000,5,0,0],[29900,3,0,1]],[[30100,4,1,0]],"
        + std::to_string(tsNs)
        + ",\"BTCUSDT\",\"binance\",\"futures_usd\","
        + std::to_string(updateId)
        + "," + std::to_string(firstUpdateId)
        + "," + std::to_string(tsNs)
        + "," + std::to_string(tsNs + 10)
        + ",1,"
        + std::to_string(captureSeq)
        + "," + std::to_string(ingestSeq)
        + "," + std::to_string(updateId)
        + "," + std::to_string(firstUpdateId)
        + ",\"initial\",\"rest_orderbook_snapshot\"]\n";
}

void writeManifest(const fs::path& dir,
                   bool tradesEnabled,
                   bool bookTickerEnabled,
                   bool orderbookEnabled,
                   std::uint64_t tradesCount,
                   std::uint64_t bookTickerCount,
                   std::uint64_t depthCount,
                   std::uint64_t snapshotCount) {
    SessionManifest manifest{};
    manifest.sessionId = "test_session";
    manifest.exchange = "binance";
    manifest.market = "futures_usd";
    manifest.symbols = {"BTCUSDT"};
    manifest.tradesEnabled = tradesEnabled;
    manifest.bookTickerEnabled = bookTickerEnabled;
    manifest.orderbookEnabled = orderbookEnabled;
    manifest.tradesCount = tradesCount;
    manifest.bookTickerCount = bookTickerCount;
    manifest.depthCount = depthCount;
    manifest.snapshotCount = snapshotCount;
    if (snapshotCount != 0u) manifest.snapshotFiles = {"snapshot_000.json"};
    writeFile(dir / "manifest.json", renderManifestJson(manifest));
}

TEST(SessionReplay, EndToEnd) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, true, 2u, 0u, 2u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 1, 10, 10));

    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],2000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,11,2,2]\n"
              "[[],[[30100,0,1,0],[30200,8,1,1]],3500,\"BTCUSDT\",\"binance\",\"futures_usd\",12,12,3,4]\n");

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",4,3]\n"
              "[30200,2,0,4000,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",5,5]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_FALSE(replay.gapDetected());
    EXPECT_EQ(replay.integritySummary().sessionHealth, hftrec::SessionHealth::Clean);
    EXPECT_TRUE(replay.integritySummary().depth.exactReplayEligible);

    EXPECT_EQ(replay.trades().size(), 2u);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.bookTickers().size(), 0u);
    ASSERT_EQ(replay.events().size(), 4u);
    ASSERT_EQ(replay.buckets().size(), 4u);
    EXPECT_EQ(replay.firstTsNs(), 2000);
    EXPECT_EQ(replay.lastTsNs(), 4000);

    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 5);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    replay.seek(2000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);

    replay.seek(5000);
    EXPECT_EQ(replay.book().asks().count(30100), 0u);
    EXPECT_EQ(replay.book().bestAskPrice(), 30200);
    EXPECT_EQ(replay.book().bestAskQty(), 8);
    EXPECT_EQ(replay.cursor(), replay.buckets().size());

    replay.seek(1000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 5);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);
    EXPECT_EQ(replay.cursor(), 0u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MissingDirectoryReturnsError) {
    SessionReplay replay{};
    EXPECT_EQ(replay.open("/this/path/does/not/exist/for/sure_xyz"), Status::InvalidArgument);
    EXPECT_NE(std::string{replay.errorDetail()}.find("session directory does not exist"), std::string::npos);
}

TEST(SessionReplay, InvalidJsonLineReportsFileAndLine) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 2u, 0u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]\n"
              "[30051,1,\"bad\",2600,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",2,2]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, DetectsDepthGapWhenSequenceIdsPresent) {
    const auto dir = makeTmpDir();
    writeManifest(dir, false, false, true, 0u, 0u, 2u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 1, 10, 10));

    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],2000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,11,2,2]\n"
              "[[],[[30100,0,1,0]],3000,\"BTCUSDT\",\"binance\",\"futures_usd\",15,15,3,3]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_TRUE(replay.gapDetected());
    EXPECT_EQ(replay.integrityFailureCount(), 1u);
    EXPECT_NE(std::string{replay.errorDetail()}.find("expected update 12"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, DetectsDepthUpdateRangeInversion) {
    const auto dir = makeTmpDir();
    writeManifest(dir, false, false, true, 0u, 0u, 1u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 1, 10, 10));

    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],2000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,12,2,2]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_TRUE(replay.gapDetected());
    EXPECT_NE(std::string{replay.errorDetail()}.find("updateId < firstUpdateId"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, DetectsNonIncreasingCaptureSequence) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 2u, 0u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",2,1]\n"
              "[30051,1,1,2600,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",2,2]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("non-increasing captureSeq"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, DetectsMixedIngestSequenceMetadata) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 2u, 0u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]\n"
              "[30051,1,1,2600,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",\"bad\"]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, SameTimestampRowsShareOneReplayBucket) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, true, true, 1u, 1u, 1u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 1, 10, 10));

    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],2000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,11,2,2]\n");

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2000,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",3,3]\n");

    writeFile(dir / "bookticker.jsonl",
              "[30000,7,30100,4,2000,\"BTCUSDT\",\"binance\",\"futures_usd\",4,4]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.events().size(), 3u);
    ASSERT_EQ(replay.buckets().size(), 1u);
    ASSERT_EQ(replay.buckets()[0].items.size(), 3u);
    EXPECT_EQ(replay.buckets()[0].tsNs, 2000);

    replay.seek(1999);
    EXPECT_EQ(replay.cursor(), 0u);
    EXPECT_EQ(replay.book().bestBidQty(), 5);

    replay.seek(2000);
    EXPECT_EQ(replay.cursor(), 1u);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, CrossChannelIngestSequenceDoesNotDegradeWhenTimestampOrderDiffers) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, true, true, 1u, 1u, 1u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 4, 10, 10));
    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],3000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,11,1,5]\n");
    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2000,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,6]\n");
    writeFile(dir / "bookticker.jsonl",
              "[30000,7,30100,4,1500,\"BTCUSDT\",\"binance\",\"futures_usd\",1,7]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_EQ(replay.integritySummary().sessionHealth, hftrec::SessionHealth::Clean);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Clean);
    EXPECT_EQ(replay.integritySummary().bookTicker.state, hftrec::ChannelHealthState::Clean);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Clean);
    EXPECT_TRUE(replay.integritySummary().incidents.empty());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MissingEnabledChannelDegradesSessionAndWritesIntegrityReport) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 1u, 0u, 0u, 0u);

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_EQ(replay.integritySummary().sessionHealth, hftrec::SessionHealth::Corrupt);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);
    EXPECT_TRUE(fs::exists(dir / "reports" / "integrity_report.json"));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, ShortDepthArrayIsCorrupt) {
    const auto dir = makeTmpDir();
    writeManifest(dir, false, false, true, 0u, 0u, 1u, 1u);

    writeFile(dir / "snapshot_000.json", makeSnapshotJson(1000, 1, 1, 10, 10));

    writeFile(dir / "depth.jsonl",
              "[[[30000,7,0,0]],[],2000,\"BTCUSDT\",\"binance\",\"futures_usd\",\"bad\",11,2,2]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_EQ(replay.integritySummary().sessionHealth, hftrec::SessionHealth::Corrupt);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
