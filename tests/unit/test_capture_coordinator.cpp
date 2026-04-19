#include <gtest/gtest.h>

#include <filesystem>

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
    config.outputDir = fs::temp_directory_path() / "hftrec_capture_coordinator_tests";
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

}  // namespace
