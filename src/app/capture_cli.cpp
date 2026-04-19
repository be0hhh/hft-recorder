#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::app {

namespace {

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder capture <trades|bookticker|orderbook> [seconds] [output_dir]");
    std::puts("  Current scope: Binance FAPI, one symbol per run, canonical JSON corpus output.");
    std::puts("");
    std::puts("Examples:");
    std::puts("  hft-recorder capture bookticker 10 ./recordings");
    std::puts("  hft-recorder capture trades 30 ./recordings");
}

capture::CaptureConfig makeDefaultConfig() {
    capture::CaptureConfig config{};
    config.exchange = "binance";
    config.market = "futures_usd";
    config.symbols = {"ETHUSDT"};
    config.outputDir = "./recordings";
    config.durationSec = 10;
    config.snapshotIntervalSec = 60;
    return config;
}

}  // namespace

int runCapture(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    auto config = makeDefaultConfig();
    const std::string channel = argv[1];
    if (argc >= 3) {
        config.durationSec = std::strtoll(argv[2], nullptr, 10);
        if (config.durationSec <= 0) {
            std::fputs("capture: seconds must be > 0\n", stderr);
            return 2;
        }
    }
    if (argc >= 4) {
        config.outputDir = argv[3];
    }

    capture::CaptureCoordinator coordinator{};
    Status startStatus = Status::InvalidArgument;
    if (channel == "trades") {
        startStatus = coordinator.startTrades(config);
    } else if (channel == "bookticker") {
        startStatus = coordinator.startBookTicker(config);
    } else if (channel == "orderbook") {
        startStatus = coordinator.startOrderbook(config);
    } else {
        std::fprintf(stderr, "capture: unknown channel '%s'\n", channel.c_str());
        printUsage();
        return 2;
    }

    if (!isOk(startStatus)) {
        const auto error = coordinator.lastError();
        if (!error.empty()) {
            std::fprintf(stderr, "capture start failed: %s\n", error.c_str());
        } else {
            std::fprintf(stderr, "capture start failed: %s\n", statusToString(startStatus).data());
        }
        return 1;
    }

    std::printf("capture started: channel=%s exchange=binance market=futures_usd symbol=ETHUSDT duration=%llds dir=%s\n",
                channel.c_str(),
                static_cast<long long>(config.durationSec),
                config.outputDir.string().c_str());

    std::this_thread::sleep_for(std::chrono::seconds(config.durationSec));
    const auto finalizeStatus = coordinator.finalizeSession();
    if (!isOk(finalizeStatus)) {
        const auto error = coordinator.lastError();
        if (!error.empty()) {
            std::fprintf(stderr, "capture finalize failed: %s\n", error.c_str());
        } else {
            std::fprintf(stderr, "capture finalize failed: %s\n", statusToString(finalizeStatus).data());
        }
        return 1;
    }

    std::puts("capture finished");
    return 0;
}

}  // namespace hftrec::app
