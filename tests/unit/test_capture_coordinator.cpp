#include <gtest/gtest.h>

#include <fstream>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>

#include "core/capture/CaptureCoordinator.hpp"
#include "hft_trader/runtime/diagnostics/RuntimeTestHooks.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::Status;
using hftrec::capture::CaptureConfig;
using hftrec::capture::CaptureCoordinator;
using hftrec::capture::LiveCacheMode;

bool mockFetchMetadata(cxet::UnifiedRequestBuilder& builder,
                       MessageBuffer&,
                       ExchangeId,
                       canon::MarketType,
                       cxet::composite::InstrumentInfoRow* out,
                       std::size_t cap,
                       std::size_t* outCount,
                       const char**) noexcept {
    if (!out || cap == 0u || !outCount) return false;
    out[0] = cxet::composite::InstrumentInfoRow{};
    out[0].symbol = builder.symbol();
    std::memcpy(out[0].tickSize, "0.1", 4u);
    std::memcpy(out[0].stepSize, "0.001", 6u);
    std::memcpy(out[0].lotSize, "0.001", 6u);
    std::memcpy(out[0].contractBaseQty, "0.001", 6u);
    std::memcpy(out[0].priceBasisQty, "100", 4u);
    *outCount = 1u;
    return true;
}

struct RuntimeHooksGuard {
    explicit RuntimeHooksGuard(const hft_trader::runtime::RuntimeTestHooks* hooks) noexcept {
        hft_trader::runtime::setRuntimeTestHooks(hooks);
    }

    ~RuntimeHooksGuard() {
        hft_trader::runtime::setRuntimeTestHooks(nullptr);
    }
};

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
    config.exchange = "not_an_exchange";

    EXPECT_EQ(coordinator.ensureSession(config), Status::InvalidArgument);
    EXPECT_NE(coordinator.lastError().find("capture exchange must be one of"), std::string::npos);
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
    EXPECT_NE(coordinator.lastError().find("different exchange/market/symbol/env/api/output directory"), std::string::npos);

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

TEST(CaptureCoordinator, PreservesEmptyFinalizedSessionManifest) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);

    const auto sessionDir = coordinator.sessionDirCopy();
    const auto manifestPath = sessionDir / "manifest.json";
    ASSERT_EQ(coordinator.finalizeSession(), Status::Ok);

    ASSERT_TRUE(fs::exists(manifestPath));
    std::ifstream manifestStream(manifestPath);
    ASSERT_TRUE(manifestStream.is_open());
    const std::string manifest((std::istreambuf_iterator<char>(manifestStream)), std::istreambuf_iterator<char>());
    EXPECT_NE(manifest.find("\"session_status\": \"failed_empty\""), std::string::npos);
    EXPECT_NE(manifest.find("\"session_health\": \"degraded\""), std::string::npos);
    EXPECT_NE(manifest.find("no canonical rows captured"), std::string::npos);

    std::error_code ec;
    fs::remove_all(config.outputDir, ec);
}

TEST(CaptureCoordinator, DisablesLiveCacheByDefault) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);
    EXPECT_EQ(coordinator.eventSource(), nullptr);
    EXPECT_EQ(coordinator.hotCache(), nullptr);
    EXPECT_TRUE(coordinator.liveEventsCopy().trades.empty());

    std::error_code ec;
    coordinator.finalizeSession();
    fs::remove_all(config.outputDir, ec);
}

TEST(CaptureCoordinator, ExposesLiveCacheWhenExplicitlyEnabled) {
    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();
    config.liveCacheMode = LiveCacheMode::Full;

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);
    EXPECT_NE(coordinator.eventSource(), nullptr);
    EXPECT_NE(coordinator.hotCache(), nullptr);

    std::error_code ec;
    coordinator.finalizeSession();
    fs::remove_all(config.outputDir, ec);
}

TEST(CaptureCoordinator, WritesInstrumentMetadataFromTraderRuntime) {
    hft_trader::runtime::RuntimeTestHooks hooks{};
    hooks.fetchMetadata = mockFetchMetadata;
    RuntimeHooksGuard guard{&hooks};

    CaptureCoordinator coordinator{};
    auto config = makeValidConfig();

    ASSERT_EQ(coordinator.ensureSession(config), Status::Ok);

    const auto metadataPath = coordinator.sessionDirCopy() / "instrument_metadata.json";
    ASSERT_TRUE(fs::exists(metadataPath));
    std::ifstream metadataStream(metadataPath);
    ASSERT_TRUE(metadataStream.is_open());
    const std::string metadata((std::istreambuf_iterator<char>(metadataStream)), std::istreambuf_iterator<char>());
    EXPECT_NE(metadata.find("\"metadata_source\": \"recorder_inference\""), std::string::npos);
    EXPECT_NE(metadata.find("\"metadata_warning\": \"hft_trader_metadata_deferred_startup_nonblocking\""), std::string::npos);

    std::error_code ec;
    coordinator.finalizeSession();
    fs::remove_all(config.outputDir, ec);
}

}  // namespace
