#include <gtest/gtest.h>

#include <algorithm>

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

void writeManifest(const fs::path& dir,
                   bool tradesEnabled,
                   bool bookTickerEnabled,
                   bool orderbookEnabled,
                   std::uint64_t tradesCount,
                   std::uint64_t bookTickerCount,
                   std::uint64_t depthCount) {
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
    writeFile(dir / "manifest.json", renderManifestJson(manifest));
}

TEST(SessionReplay, EndToEnd) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, true, 2u, 0u, 2u);

    writeFile(dir / "depth.jsonl",
              "[[30000,7,0],[30100,4,1],2000]\n"
              "[[30100,0,1],[30200,8,1],3500]\n");

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500]\n"
              "[30200,2,0,4000]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_FALSE(replay.sequenceValidationAvailable());
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
    EXPECT_EQ(replay.book().bestBidPrice(), 0);
    EXPECT_EQ(replay.book().bestBidQty(), 0);
    EXPECT_EQ(replay.book().bestAskPrice(), 0);
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
    writeManifest(dir, true, false, false, 2u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500]\n"
              "[30051,1,\"bad\",2600]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MinimalRowsHaveNoSequenceValidation) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 2u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500]\n"
              "[30051,1,1,2600]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::Ok);
    EXPECT_FALSE(replay.sequenceValidationAvailable());
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Clean);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, RejectsMalformedMinimalTradeLine) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 2u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500]\n"
              "[30051,1,1,\"bad\"]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, PartialDepthTapeSidecarLoadKeepsValidPrefix) {
    const auto dir = makeTmpDir();
    fs::create_directories(dir / "jsonl");
    writeManifest(dir, false, false, true, 0u, 0u, 3u);

    writeFile(dir / "jsonl" / "depth_tape.jsonl",
              "[10936540037604775808,30000,10,30001,20]\n"
              "[10936540037704775808,30002,30]\n"
              "[10936540037804775808,30003,40]\n");
    writeFile(dir / "jsonl" / "depth_sidecar.jsonl",
              "[10936540037604775808,0,1,1,1]\n"
              "[10936540037704775808,0,1]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.addDepthFileAllowPartial(dir / "jsonl" / "depth_tape.jsonl"), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("line count mismatch at line 3"), std::string::npos);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Corrupt);
    EXPECT_FALSE(replay.integritySummary().depth.exactReplayEligible);

    replay.finalize();
    EXPECT_EQ(replay.status(), Status::Ok);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.events().size(), 2u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, RejectsLegacyExtendedTradeRows) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, false, false, 1u, 0u, 0u);

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2500,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 1"), std::string::npos);
    EXPECT_EQ(replay.integritySummary().trades.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, KeepsCandlesAndCandles2Separate) {
    const auto dir = makeTmpDir();
    writeManifest(dir, false, false, false, 0u, 0u, 0u);

    writeFile(dir / "candles.jsonl",
              "[1,1000,10000000000,10000000000,10000000000,10000000000,0,0]\n");
    writeFile(dir / "candles2.jsonl",
              "[1,2000,20000000000,20000000000,20000000000,20000000000,0,0]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.candles().size(), 1u);
    ASSERT_EQ(replay.candles2().size(), 1u);
    EXPECT_EQ(replay.candles().front().tsNs, 1000);
    EXPECT_EQ(replay.candles2().front().tsNs, 2000);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, SameTimestampRowsShareOneReplayBucket) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, true, true, 1u, 1u, 1u);

    writeFile(dir / "depth.jsonl",
              "[[30000,7,0],[30100,4,1],2000]\n");

    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2000]\n");

    writeFile(dir / "bookticker.jsonl",
              "[30000,7,30100,4,2000]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.events().size(), 3u);
    ASSERT_EQ(replay.buckets().size(), 1u);
    ASSERT_EQ(replay.buckets()[0].items.size(), 3u);
    EXPECT_EQ(replay.buckets()[0].tsNs, 2000);

    replay.seek(1999);
    EXPECT_EQ(replay.cursor(), 0u);
    EXPECT_EQ(replay.book().bestBidQty(), 0);

    replay.seek(2000);
    EXPECT_EQ(replay.cursor(), 1u);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, OpenLoadsReferenceChannelsFromSessionCorpus) {
    const auto dir = makeTmpDir();
    fs::create_directories(dir / "jsonl");

    SessionManifest manifest{};
    manifest.sessionId = "reference_session";
    manifest.exchange = "binance";
    manifest.market = "futures_usd";
    manifest.symbols = {"BTCUSDT"};
    manifest.tradesEnabled = false;
    manifest.liquidationsEnabled = false;
    manifest.bookTickerEnabled = false;
    manifest.orderbookEnabled = false;
    manifest.markPriceEnabled = true;
    manifest.indexPriceEnabled = true;
    manifest.fundingEnabled = true;
    manifest.priceLimitEnabled = true;
    manifest.markPriceCount = 1u;
    manifest.indexPriceCount = 1u;
    manifest.fundingCount = 1u;
    manifest.priceLimitCount = 1u;
    writeFile(dir / "manifest.json", renderManifestJson(manifest));
    writeFile(dir / "jsonl" / "mark_price.jsonl", "[2000,30000]\n");
    writeFile(dir / "jsonl" / "index_price.jsonl", "[2100,29990]\n");
    writeFile(dir / "jsonl" / "funding.jsonl", "[2200,125,2000,2400]\n");
    writeFile(dir / "jsonl" / "price_limit.jsonl", "[2300,31000,29000,1]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.markPrices().size(), 1u);
    ASSERT_EQ(replay.indexPrices().size(), 1u);
    ASSERT_EQ(replay.fundings().size(), 1u);
    ASSERT_EQ(replay.priceLimits().size(), 1u);
    EXPECT_EQ(replay.markPrices()[0].markPriceE8, 30000);
    EXPECT_EQ(replay.indexPrices()[0].indexPriceE8, 29990);
    EXPECT_EQ(replay.fundings()[0].fundingRateE8, 125);
    EXPECT_EQ(replay.priceLimits()[0].buyLimitE8, 31000);
    EXPECT_EQ(replay.events().size(), 4u);
    EXPECT_EQ(replay.buckets().size(), 4u);
    EXPECT_EQ(replay.firstTsNs(), 2000);
    EXPECT_EQ(replay.lastTsNs(), 2300);
    EXPECT_EQ(replay.loadReport().markPriceState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(replay.loadReport().indexPriceState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(replay.loadReport().fundingState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(replay.loadReport().priceLimitState, hftrec::corpus::ChannelLoadState::Clean);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, CrossChannelIngestSequenceDoesNotDegradeWhenTimestampOrderDiffers) {
    const auto dir = makeTmpDir();
    writeManifest(dir, true, true, true, 1u, 1u, 1u);

    writeFile(dir / "depth.jsonl",
              "[[30000,7,0],[30100,4,1],3000]\n");
    writeFile(dir / "trades.jsonl",
              "[30050,1,1,2000]\n");
    writeFile(dir / "bookticker.jsonl",
              "[30000,7,30100,4,1500]\n");

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
    writeManifest(dir, true, false, false, 1u, 0u, 0u);

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
    writeManifest(dir, false, false, true, 0u, 0u, 1u);

    writeFile(dir / "depth.jsonl",
              "[[30000,7],2000]\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_EQ(replay.integritySummary().sessionHealth, hftrec::SessionHealth::Corrupt);
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Corrupt);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, NormalizesBitgetFixedDepthSnapshotsOnLoad) {
    const auto dir = makeTmpDir();

    SessionManifest manifest{};
    manifest.sessionId = "bitget_session";
    manifest.exchange = "bitget";
    manifest.market = "futures_usd";
    manifest.symbols = {"BSBUSDT"};
    manifest.tradesEnabled = false;
    manifest.bookTickerEnabled = false;
    manifest.orderbookEnabled = true;
    manifest.depthCount = 2u;
    writeFile(dir / "manifest.json", renderManifestJson(manifest));

    writeFile(dir / "depth.jsonl",
              "[[100,5,0],[101,4,0],[110,3,1],2000]\n"
              "[[101,7,0],[111,2,1],3000]\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    ASSERT_EQ(replay.depths().size(), 2u);

    const auto& second = replay.depths()[1].levels;
    const auto hasDelete = [&](std::int64_t price, std::uint8_t side) {
        return std::find_if(second.begin(), second.end(), [&](const auto& level) {
            return level.priceE8 == price && level.qtyE8 == 0 && level.side == side;
        }) != second.end();
    };
    EXPECT_TRUE(hasDelete(100, 0));
    EXPECT_TRUE(hasDelete(110, 1));

    replay.seek(3000);
    EXPECT_EQ(replay.book().bids().count(100), 0u);
    EXPECT_EQ(replay.book().asks().count(110), 0u);
    EXPECT_EQ(replay.book().bestBidPrice(), 101);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    EXPECT_EQ(replay.book().bestAskPrice(), 111);
    EXPECT_EQ(replay.book().bestAskQty(), 2);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
}  // namespace
