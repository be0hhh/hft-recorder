#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
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

TEST(SessionReplay, EndToEnd) {
    const auto dir = makeTmpDir();

    writeFile(dir / "snapshot_000.json",
              "{\n"
              "  \"tsNs\": 1000,\n"
              "  \"updateId\": 10,\n"
              "  \"firstUpdateId\": 10,\n"
              "  \"bids\": [{\"price_i64\":30000,\"qty_i64\":5},{\"price_i64\":29900,\"qty_i64\":3}],\n"
              "  \"asks\": [{\"price_i64\":30100,\"qty_i64\":4}]\n"
              "}\n");

    writeFile(dir / "depth.jsonl",
              "{\"tsNs\":2000,\"updateId\":11,\"firstUpdateId\":11,\"bids\":[{\"price_i64\":30000,\"qty_i64\":7}],\"asks\":[]}\n"
              "{\"tsNs\":3500,\"updateId\":12,\"firstUpdateId\":12,\"bids\":[],\"asks\":[{\"price_i64\":30100,\"qty_i64\":0},{\"price_i64\":30200,\"qty_i64\":8}]}\n");

    writeFile(dir / "trades.jsonl",
              "{\"tsNs\":2500,\"priceE8\":30050,\"qtyE8\":1,\"sideBuy\":1}\n"
              "{\"tsNs\":4000,\"priceE8\":30200,\"qtyE8\":2,\"sideBuy\":0}\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_FALSE(replay.gapDetected());

    EXPECT_EQ(replay.trades().size(), 2u);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.bookTickers().size(), 0u);
    ASSERT_EQ(replay.events().size(), 4u);
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
    EXPECT_EQ(replay.cursor(), replay.events().size());

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
    EXPECT_EQ(replay.open("/this/path/does/not/exist/for/sure_xyz"),
              Status::InvalidArgument);
    EXPECT_NE(std::string{replay.errorDetail()}.find("session directory does not exist"), std::string::npos);
}

TEST(SessionReplay, MinimalDepthFormatHasNoWarnings) {
    const auto dir = makeTmpDir();

    writeFile(dir / "snapshot_000.json",
              "{\n"
              "  \"tsNs\": 1000,\n"
              "  \"updateId\": 10,\n"
              "  \"firstUpdateId\": 10,\n"
              "  \"bids\": [{\"price_i64\":30000,\"qty_i64\":5}],\n"
              "  \"asks\": [{\"price_i64\":30100,\"qty_i64\":4}]\n"
              "}\n");

    writeFile(dir / "depth.jsonl",
              "{\"tsNs\":2000,\"updateId\":11,\"firstUpdateId\":11,\"bids\":[{\"price_i64\":30000,\"qty_i64\":7}],\"asks\":[]}\n"
              "{\"tsNs\":3000,\"updateId\":12,\"firstUpdateId\":12,\"bids\":[],\"asks\":[{\"price_i64\":30100,\"qty_i64\":0}]}\n");

    writeFile(dir / "trades.jsonl",
              "{\"tsNs\":2500,\"priceE8\":30050,\"qtyE8\":1,\"sideBuy\":1}\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::Ok);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.trades().size(), 1u);
    EXPECT_TRUE(replay.errorDetail().empty());

    replay.seek(3000);
    EXPECT_EQ(replay.book().asks().count(30100), 0u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, InvalidJsonLineReportsFileAndLine) {
    const auto dir = makeTmpDir();

    writeFile(dir / "trades.jsonl",
              "{\"tsNs\":2500,\"priceE8\":30050,\"qtyE8\":1,\"sideBuy\":1}\n"
              "{\"tsNs\":2600,\"priceE8\":30051,\"qtyE8\":1,\"sideBuy\":\"bad\"}\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_NE(std::string{replay.errorDetail()}.find("trades.jsonl line 2"), std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, DetectsDepthGapWhenSequenceIdsPresent) {
    const auto dir = makeTmpDir();

    writeFile(dir / "snapshot_000.json",
              "{\n"
              "  \"tsNs\": 1000,\n"
              "  \"updateId\": 10,\n"
              "  \"firstUpdateId\": 10,\n"
              "  \"bids\": [{\"price_i64\":30000,\"qty_i64\":5}],\n"
              "  \"asks\": [{\"price_i64\":30100,\"qty_i64\":4}]\n"
              "}\n");

    writeFile(dir / "depth.jsonl",
              "{\"tsNs\":2000,\"updateId\":11,\"firstUpdateId\":11,\"bids\":[{\"price_i64\":30000,\"qty_i64\":7}],\"asks\":[]}\n"
              "{\"tsNs\":3000,\"updateId\":15,\"firstUpdateId\":15,\"bids\":[],\"asks\":[{\"price_i64\":30100,\"qty_i64\":0}]}\n");

    SessionReplay replay{};
    EXPECT_EQ(replay.open(dir), Status::CorruptData);
    EXPECT_TRUE(replay.sequenceValidationAvailable());
    EXPECT_TRUE(replay.gapDetected());
    EXPECT_EQ(replay.integrityFailureCount(), 1u);
    EXPECT_NE(std::string{replay.errorDetail()}.find("expected update 12"), std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
