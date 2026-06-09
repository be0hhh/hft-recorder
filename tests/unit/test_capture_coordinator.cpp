#include <gtest/gtest.h>

#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <iterator>

#include "core/capture/CaptureCoordinator.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
using hftrec::capture::CaptureConfig;
using hftrec::capture::CaptureCoordinator;

CaptureConfig makeValidConfig() {
    CaptureConfig config{};
    config.exchange = "binance";
    config.market = "futures_usd";
    config.symbols = {"ETHUSDT"};
    config.outputDir = fs::temp_directory_path()
        / ("hftrec_capture_coordinator_tests_" + std::to_string(std::rand()));
    return config;
}

TEST(CaptureCoordinator, RejectsUnsupportedExchange) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();
    config.exchange = "okx";

    EXPECT_EQ(coordinator.ensureSession(config), Status::InvalidArgument);
    EXPECT_NE(coordinator.lastError().find("exchange=binance"), std::string::npos);
}

TEST(CaptureCoordinator, RejectsMultipleSymbolsPerCoordinator) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();
    config.symbols = {"ETHUSDT", "BTCUSDT"};

    EXPECT_EQ(coordinator.ensureSession(config), Status::InvalidArgument);
    EXPECT_NE(coordinator.lastError().find("exactly one symbol"), std::string::npos);
}

TEST(CaptureCoordinator, RejectsConfigDriftWhileSessionIsOpen) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);

    auto mismatchedConfig = config;
    mismatchedConfig.symbols = {"BTCUSDT"};

    EXPECT_EQ(coordinator.ensureSession(mismatchedConfig), Status::InvalidArgument);
    EXPECT_NE(coordinator.lastError().find("different exchange/market/symbol/output directory"), std::string::npos);

    std::error_code ec;
    coordinator.finalizeSession();
    fs::remove_all(config.outputDir, ec);
}

TEST(CaptureCoordinator, WritesManifestAsSoonAsSessionIsEnsured) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);

    const auto sessionDir = coordinator.sessionDirCopy();
    const auto manifestPath = sessionDir / "manifest.json";
    ASSERT_TRUE(fs::exists(manifestPath));
    std::ifstream manifestStream(manifestPath);
    ASSERT_TRUE(manifestStream.is_open());
    const std::string manifest((std::istreambuf_iterator<char>(manifestStream)), std::istreambuf_iterator<char>());
    EXPECT_NE(manifest.find("\"session_status\": \"recording\""), std::string::npos);

    std::error_code ec;
    coordinator.finalizeSession();
    fs::remove_all(config.outputDir, ec);
}

}  // namespace
