#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/common/Status.hpp"
#include "hftrec/backtest.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureSession(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

fs::path makeTempSession() {
    const auto source = fixtureSession("clean_full");
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto target = fs::temp_directory_path() / fs::path("hftrec_backtest_runner_unit_" + std::to_string(suffix));
    std::error_code ec;
    fs::remove_all(target, ec);
    ec.clear();
    fs::create_directories(target, ec);
    EXPECT_FALSE(ec) << ec.message();
    ec.clear();
    fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    EXPECT_FALSE(ec) << ec.message();
    return target;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(BacktestRunner, WritesSkeletonRunResultFromSessionReplay) {
    const fs::path session = makeTempSession();

    hftrec::BacktestRunRequest request{};
    request.sessionPath = session;
    request.requestId = "request-unit";
    request.runId = "run-unit";
    request.strategy = "spread_maker1and2";

    const auto result = hftrec::runBacktest(request);

    EXPECT_EQ(result.status, hftrec::Status::Ok);
    EXPECT_EQ(result.runId, "run-unit");
    EXPECT_EQ(result.strategy, "spread_maker1and2");
    EXPECT_GT(result.events, 0u);
    EXPECT_TRUE(fs::is_directory(result.resultPath));
    EXPECT_FALSE(fs::exists(result.resultPath / "orders.jsonl"));
    EXPECT_TRUE(fs::exists(result.resultPath / "order_lifetimes.jsonl"));
    EXPECT_TRUE(fs::exists(result.resultPath / "fills.jsonl"));
    EXPECT_TRUE(fs::exists(result.resultPath / "equity.jsonl"));

    const std::string json = readFile(result.resultPath / "manifest.json");
    EXPECT_NE(json.find("\"type\": \"run.result.v2\""), std::string::npos);
    EXPECT_NE(json.find("\"status\": \"complete\""), std::string::npos);
    EXPECT_NE(json.find("\"strategy\": \"spread_maker1and2\""), std::string::npos);
    EXPECT_NE(json.find("\"order_lifetimes\""), std::string::npos);
}
