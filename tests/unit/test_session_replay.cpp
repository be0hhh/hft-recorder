#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
using hftrec::isOk;
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
    // Build a tiny session on disk: one snapshot, two depth deltas, two trades.
    const auto dir = makeTmpDir();

    writeFile(dir / "snapshot_000.json",
              "{\n"
              "  \"session_id\": \"t\",\n"
              "  \"channel\": \"snapshot\",\n"
              "  \"exchange\": \"b\",\n"
              "  \"market\": \"m\",\n"
              "  \"symbol\": \"X\",\n"
              "  \"snapshot_index\": 0,\n"
              "  \"snapshot_time_ns\": 1000,\n"
              "  \"bids\": [{\"price_i64\":30000,\"qty_i64\":5},{\"price_i64\":29900,\"qty_i64\":3}],\n"
              "  \"asks\": [{\"price_i64\":30100,\"qty_i64\":4}]\n"
              "}\n");

    writeFile(dir / "depth.jsonl",
              "{\"session_id\":\"t\",\"channel\":\"depth\",\"exchange\":\"b\",\"market\":\"m\",\"symbol\":\"X\","
              "\"event_index\":1,\"event_time_ns\":2000,\"first_update_id\":1,\"final_update_id\":2,"
              "\"bids\":[{\"price_i64\":30000,\"qty_i64\":7}],"
              "\"asks\":[]}\n"
              "{\"session_id\":\"t\",\"channel\":\"depth\",\"exchange\":\"b\",\"market\":\"m\",\"symbol\":\"X\","
              "\"event_index\":2,\"event_time_ns\":3500,\"first_update_id\":3,\"final_update_id\":3,"
              "\"bids\":[],"
              "\"asks\":[{\"price_i64\":30100,\"qty_i64\":0},{\"price_i64\":30200,\"qty_i64\":8}]}\n");

    writeFile(dir / "trades.jsonl",
              "{\"session_id\":\"t\",\"channel\":\"trades\",\"exchange\":\"b\",\"market\":\"m\",\"symbol\":\"X\","
              "\"event_index\":1,\"event_time_ns\":2500,\"trade_time_ns\":2500,\"trade_id\":1,"
              "\"price_i64\":30050,\"qty_i64\":1,\"side\":\"buy\",\"is_aggregated\":true,\"is_buyer_maker\":false}\n"
              "{\"session_id\":\"t\",\"channel\":\"trades\",\"exchange\":\"b\",\"market\":\"m\",\"symbol\":\"X\","
              "\"event_index\":2,\"event_time_ns\":4000,\"trade_time_ns\":4000,\"trade_id\":2,"
              "\"price_i64\":30200,\"qty_i64\":2,\"side\":\"sell\",\"is_aggregated\":true,\"is_buyer_maker\":true}\n");

    SessionReplay replay{};
    ASSERT_EQ(replay.open(dir), Status::Ok);

    EXPECT_EQ(replay.trades().size(), 2u);
    EXPECT_EQ(replay.depths().size(), 2u);
    EXPECT_EQ(replay.bookTickers().size(), 0u);
    ASSERT_EQ(replay.events().size(), 4u);
    EXPECT_EQ(replay.firstTsNs(), 2000);
    EXPECT_EQ(replay.lastTsNs(), 4000);

    // Initially book reflects snapshot.
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(),   5);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    // Seek to ts=2000 → apply first delta.
    replay.seek(2000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(),   7);      // modified to 7

    // Seek to ts=5000 → apply all events.
    replay.seek(5000);
    // After second delta: ask 30100 removed, ask 30200 added (qty 8).
    EXPECT_EQ(replay.book().asks().count(30100), 0u);
    EXPECT_EQ(replay.book().bestAskPrice(), 30200);
    EXPECT_EQ(replay.book().bestAskQty(),   8);
    EXPECT_EQ(replay.cursor(), replay.events().size());

    // Rewind: seek to ts=1000 — book returns to snapshot state.
    replay.seek(1000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestBidQty(),   5);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);
    EXPECT_EQ(replay.cursor(), 0u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(SessionReplay, MissingDirectoryReturnsError) {
    SessionReplay replay{};
    EXPECT_EQ(replay.open("/this/path/does/not/exist/for/sure_xyz"),
              Status::InvalidArgument);
}

}  // namespace
