#include <gtest/gtest.h>

#include <filesystem>

#include "core/corpus/CorpusLoader.hpp"
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

TEST(SessionReplay, FixtureDepthGapIsReflectedInLoadReport) {
    hftrec::replay::SessionReplay replay{};
    ASSERT_EQ(replay.open(fixtureDir("corrupt_depth_gap")), hftrec::Status::CorruptData);
    EXPECT_TRUE(replay.gapDetected());
    ASSERT_FALSE(replay.loadReport().issues.empty());
    EXPECT_EQ(replay.loadReport().issues.back().code, hftrec::corpus::LoadIssueCode::DepthGapDetected);
}

}  // namespace
