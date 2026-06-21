#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <string_view>

#include "core/capture/SessionManifest.hpp"
#include "core/corpus/CorpusLoader.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureDir(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

TEST(CorpusLoader, CleanFixtureLoadsAndUsesSeekIndex) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("clean_full"), corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.manifestPresent);
    EXPECT_TRUE(report.usedSeekIndex);
    EXPECT_FALSE(report.staleSeekIndex);
    EXPECT_EQ(report.tradesState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.bookTickerState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.depthState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.snapshotState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_GE(corpus.tradeLines.size(), 1u);
    EXPECT_GE(corpus.bookTickerLines.size(), 1u);
    EXPECT_GE(corpus.depthLines.size(), 2u);
    EXPECT_EQ(corpus.snapshotDocuments.size(), 1u);
}

TEST(InstrumentMetadata, RoundTripsTraderBacktestGridFields) {
    auto metadata = hftrec::corpus::makeInstrumentMetadata("binance", "futures", "BTCUSDT");
    metadata.tickSizeE8 = 10000000;
    metadata.lotSizeE8 = 100000;
    metadata.contractBaseQtyE8 = 100000;
    metadata.priceBasisQtyE8 = 10000000000LL;
    metadata.expiryUtcNs = 1781913600000000000LL;
    metadata.tickSizeSource = "hft_trader_exchange_info";
    metadata.lotSizeSource = "hft_trader_exchange_info";
    metadata.contractBaseQtySource = "hft_trader_exchange_info";
    metadata.priceBasisQtySource = "hft_trader_exchange_info";
    metadata.expiryUtcNsSource = "hft_trader_exchange_info";
    metadata.metadataSource = "hft_trader";

    const auto document = hftrec::corpus::renderInstrumentMetadataJson(metadata);
    EXPECT_NE(document.find("\"contract_base_qty_e8\": 100000"), std::string::npos);
    EXPECT_NE(document.find("\"price_basis_qty_e8\": 10000000000"), std::string::npos);
    EXPECT_NE(document.find("\"expiry_utc_ns\": 1781913600000000000"), std::string::npos);
    EXPECT_NE(document.find("\"metadata_source\": \"hft_trader\""), std::string::npos);

    hftrec::corpus::InstrumentMetadata parsed{};
    ASSERT_EQ(hftrec::corpus::parseInstrumentMetadataJson(document, parsed), hftrec::Status::Ok);
    ASSERT_TRUE(parsed.contractBaseQtyE8.has_value());
    EXPECT_EQ(*parsed.contractBaseQtyE8, 100000);
    EXPECT_EQ(parsed.contractBaseQtySource, "hft_trader_exchange_info");
    ASSERT_TRUE(parsed.priceBasisQtyE8.has_value());
    EXPECT_EQ(*parsed.priceBasisQtyE8, 10000000000LL);
    EXPECT_EQ(parsed.priceBasisQtySource, "hft_trader_exchange_info");
    ASSERT_TRUE(parsed.expiryUtcNs.has_value());
    EXPECT_EQ(*parsed.expiryUtcNs, 1781913600000000000LL);
    EXPECT_EQ(parsed.expiryUtcNsSource, "hft_trader_exchange_info");
    EXPECT_EQ(parsed.metadataSource, "hft_trader");
}

TEST(InstrumentMetadata, ParsesLegacyDocumentWithoutPriceBasisAsIdentityMissing) {
    constexpr std::string_view document = R"({
  "schema_version": "hftrec.instrument_metadata.v1",
  "exchange": "finam",
  "market": "spot",
  "symbol": "SBER@MISX",
  "metadata_source": "recorder_inference"
})";

    hftrec::corpus::InstrumentMetadata parsed{};
    ASSERT_EQ(hftrec::corpus::parseInstrumentMetadataJson(document, parsed), hftrec::Status::Ok);
    EXPECT_FALSE(parsed.priceBasisQtyE8.has_value());
    EXPECT_EQ(parsed.priceBasisQtySource, "unknown");
    EXPECT_FALSE(parsed.expiryUtcNs.has_value());
    EXPECT_EQ(parsed.expiryUtcNsSource, "unknown");
}

TEST(CorpusLoader, CorruptJsonFixtureReportsArtifactAndLine) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("corrupt_bad_json_line"), corpus, report), hftrec::Status::CorruptData);
    ASSERT_FALSE(report.issues.empty());
    EXPECT_EQ(report.issues.front().code, hftrec::corpus::LoadIssueCode::InvalidJsonLine);
    EXPECT_EQ(report.issues.front().artifact, "trades.jsonl");
    EXPECT_EQ(report.issues.front().lineOrRow, 2u);
}

TEST(CorpusLoader, UnsupportedSchemaFixtureFailsDeterministically) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("corrupt_schema_mismatch"), corpus, report), hftrec::Status::CorruptData);
    ASSERT_FALSE(report.issues.empty());
    EXPECT_EQ(report.issues.front().code, hftrec::corpus::LoadIssueCode::UnsupportedSchemaVersion);
}

TEST(CorpusLoader, StaleSeekIndexDegradesButStillLoads) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("stale_seek_index"), corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.staleSeekIndex);
    EXPECT_FALSE(report.usedSeekIndex);
    EXPECT_EQ(report.seekIndexState, hftrec::corpus::ChannelLoadState::Degraded);
}

TEST(CorpusLoader, RecordingManifestDoesNotRequireFinalSupportArtifacts) {
    const fs::path sessionDir = fs::temp_directory_path() / "hftrec_recording_manifest_loader_test";
    std::error_code ec;
    fs::remove_all(sessionDir, ec);
    ASSERT_TRUE(fs::create_directories(sessionDir, ec));
    ASSERT_FALSE(ec);

    hftrec::capture::SessionManifest manifest{};
    manifest.sessionId = "hftrec_recording_manifest_loader_test";
    manifest.exchange = "binance";
    manifest.market = "futures";
    manifest.symbols = {"ETHUSDT"};
    manifest.selectedParentDir = sessionDir.parent_path().string();
    manifest.startedAtNs = 1000;
    manifest.sessionStatus = "recording";
    manifest.canonicalArtifacts = {"manifest.json", manifest.instrumentMetadataPath};
    manifest.supportArtifacts = {};

    std::ofstream out(sessionDir / "manifest.json");
    ASSERT_TRUE(out.is_open());
    out << hftrec::capture::renderManifestJson(manifest);
    out.close();

    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};
    EXPECT_EQ(loader.loadDetailed(sessionDir, corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.manifestPresent);
    EXPECT_EQ(corpus.manifest.sessionStatus, "recording");

    fs::remove_all(sessionDir, ec);
}

TEST(SessionReplay, FixtureReplayUsesSharedLoaderVerdict) {
    hftrec::replay::SessionReplay replay{};
    ASSERT_EQ(replay.open(fixtureDir("clean_full")), hftrec::Status::Ok);
    EXPECT_TRUE(replay.loadReport().usedSeekIndex);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    replay.seek(3000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    EXPECT_EQ(replay.book().bestAskPrice(), 30200);

    replay.seek(1000);
    EXPECT_EQ(replay.book().bestBidQty(), 5);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);
}

TEST(SessionReplay, MinimalDepthFixtureDoesNotInferRemovedUpdateIdGaps) {
    hftrec::replay::SessionReplay replay{};
    ASSERT_EQ(replay.open(fixtureDir("corrupt_depth_gap")), hftrec::Status::Ok);
    EXPECT_FALSE(replay.gapDetected());
    EXPECT_EQ(replay.integritySummary().depth.state, hftrec::ChannelHealthState::Clean);
}

}  // namespace
