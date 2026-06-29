#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/capture/Candles2BulkState.hpp"

namespace {

TEST(Candles2BulkState, RendersAndParsesProgressCheckpoint) {
    hftrec::capture::Candles2BulkState state{};
    state.status = "partial";
    state.exchange = "finam";
    state.market = "spot";
    state.symbol = "GAZP@MISX";
    state.timeframe = "1m";
    state.candles2Path = "jsonl/candles2_1m.jsonl";
    state.compatibilityCandlesPath = "jsonl/candles_1m.jsonl";
    state.requestedLimit = 1'000'000u;
    state.pageLimit = 100u;
    state.requestedEndNs = 1782728169000000000LL;
    state.cursorEndNs = 1782722168999999999ULL;
    state.oldestTsNs = 1782722169000000000ULL;
    state.newestTsNs = 1782728169000000000ULL;
    state.rowsWritten = 1000u;
    state.pagesOk = 10u;
    state.rowsRaw = 1000u;
    state.emptyWindowsSkipped = 2u;
    state.lastError = "grpc timeout";

    const std::string json = hftrec::capture::renderCandles2BulkStateJson(state);
    EXPECT_NE(json.find("\"schema\": \"hftrec.candles2_bulk_state.v1\""), std::string::npos);
    EXPECT_NE(json.find("\"rows_written\": 1000"), std::string::npos);
    EXPECT_NE(json.find("\"cursor_end_ns\": 1782722168999999999"), std::string::npos);

    hftrec::capture::Candles2BulkState parsed{};
    ASSERT_EQ(hftrec::capture::parseCandles2BulkStateJson(json, parsed), hftrec::Status::Ok);
    EXPECT_EQ(parsed.status, state.status);
    EXPECT_EQ(parsed.exchange, state.exchange);
    EXPECT_EQ(parsed.market, state.market);
    EXPECT_EQ(parsed.symbol, state.symbol);
    EXPECT_EQ(parsed.timeframe, state.timeframe);
    EXPECT_EQ(parsed.candles2Path, state.candles2Path);
    EXPECT_EQ(parsed.compatibilityCandlesPath, state.compatibilityCandlesPath);
    EXPECT_EQ(parsed.requestedLimit, state.requestedLimit);
    EXPECT_EQ(parsed.pageLimit, state.pageLimit);
    EXPECT_EQ(parsed.requestedEndNs, state.requestedEndNs);
    EXPECT_EQ(parsed.cursorEndNs, state.cursorEndNs);
    EXPECT_EQ(parsed.oldestTsNs, state.oldestTsNs);
    EXPECT_EQ(parsed.newestTsNs, state.newestTsNs);
    EXPECT_EQ(parsed.rowsWritten, state.rowsWritten);
    EXPECT_EQ(parsed.pagesOk, state.pagesOk);
    EXPECT_EQ(parsed.rowsRaw, state.rowsRaw);
    EXPECT_EQ(parsed.emptyWindowsSkipped, state.emptyWindowsSkipped);
    EXPECT_EQ(parsed.lastError, state.lastError);
}

TEST(Candles2BulkState, WritesCheckpointUnderReportsDirectory) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "hftrec_candles2_bulk_state_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    hftrec::capture::Candles2BulkState state{};
    state.status = "running";
    state.exchange = "finam";
    state.market = "spot";
    state.symbol = "SBER@MISX";
    state.timeframe = "5m";
    state.rowsWritten = 500u;

    std::string error;
    ASSERT_EQ(hftrec::capture::writeCandles2BulkStateFile(root, state, &error), hftrec::Status::Ok) << error;

    const auto statePath = root / hftrec::capture::kCandles2BulkStateRelativePath;
    ASSERT_TRUE(std::filesystem::exists(statePath));
    std::ifstream in(statePath, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"status\": \"running\""), std::string::npos);

    std::filesystem::remove_all(root, ec);
}

}  // namespace
