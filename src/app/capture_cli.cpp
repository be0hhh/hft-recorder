#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::app {

namespace {

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder capture <trades|liquidations|bookticker|orderbook|candles> [seconds] [output_dir] [exchange] [symbol] [trades_warmup_sec]");
    std::puts("  hft-recorder capture bookticker all [seconds] [output_dir]");
    std::puts("  Current scope: canonical JSON corpus output, one session folder per exchange/symbol.");
    std::puts("");
    std::puts("Examples:");
    std::puts("  hft-recorder capture bookticker all 60 ./recordings");
    std::puts("  hft-recorder capture bookticker 10 ./recordings binance BTCUSDT");
    std::puts("  hft-recorder capture bookticker 10 ./recordings kucoin BTCUSDTM");
    std::puts("  hft-recorder capture bookticker 10 ./recordings gate BTC_USDT");
    std::puts("  hft-recorder capture trades 30 ./recordings binance ETHUSDT 300");
    std::puts("  hft-recorder capture candles 1 ./recordings binance BSBUSDT");
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

struct VenueDefault {
    const char* exchange;
    const char* symbol;
};

constexpr VenueDefault kBookTickerVenueDefaults[] = {
    {"binance", "BTCUSDT"},
    {"kucoin", "BTCUSDTM"},
    {"gate", "BTC_USDT"},
    {"bitget", "BTCUSDT"},
};

Status startChannel(capture::CaptureCoordinator& coordinator,
                    const std::string& channel,
                    const capture::CaptureConfig& config) {
    if (channel == "trades") return coordinator.startTrades(config);
    if (channel == "liquidations" || channel == "liquidation" || channel == "forceOrder") return coordinator.startLiquidations(config);
    if (channel == "bookticker") return coordinator.startBookTicker(config);
    if (channel == "orderbook") return coordinator.startOrderbook(config);
    if (channel == "candles" || channel == "candle" || channel == "klines") {
        const auto sessionStatus = coordinator.ensureSession(config);
        if (!isOk(sessionStatus)) return sessionStatus;
        return coordinator.captureCandlesOnce(config);
    }
    return Status::InvalidArgument;
}

bool applyOptionalSingleVenueArgs(capture::CaptureConfig& config, int argc, char** argv) {
    if (argc >= 5) {
        config.exchange = argv[4];
    }
    if (argc >= 6) {
        config.symbols = {argv[5]};
    }
    if (argc >= 7) {
        config.tradesHistoryWarmupSec = std::strtoll(argv[6], nullptr, 10);
        if (config.tradesHistoryWarmupSec < 0) config.tradesHistoryWarmupSec = 0;
        if (config.tradesHistoryWarmupSec > 86400) config.tradesHistoryWarmupSec = 86400;
    }
    return true;
}

}  // namespace

int runCapture(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    auto config = makeDefaultConfig();
    const std::string channel = argv[1];
    const bool allBookTickers = channel == "bookticker" && argc >= 3 && std::string{argv[2]} == "all";
    const int secondsArgIndex = allBookTickers ? 3 : 2;
    const int outputArgIndex = allBookTickers ? 4 : 3;

    if (argc >= secondsArgIndex + 1) {
        config.durationSec = std::strtoll(argv[secondsArgIndex], nullptr, 10);
        if (config.durationSec <= 0) {
            std::fputs("capture: seconds must be > 0\n", stderr);
            return 2;
        }
    }
    if (argc >= outputArgIndex + 1) {
        config.outputDir = argv[outputArgIndex];
    }

    if (allBookTickers) {
        std::vector<std::unique_ptr<capture::CaptureCoordinator>> coordinators;
        coordinators.reserve(std::size(kBookTickerVenueDefaults));

        for (const auto& venue : kBookTickerVenueDefaults) {
            auto venueConfig = config;
            venueConfig.exchange = venue.exchange;
            venueConfig.market = "futures_usd";
            venueConfig.symbols = {venue.symbol};

            auto coordinator = std::make_unique<capture::CaptureCoordinator>();
            const auto startStatus = coordinator->startBookTicker(venueConfig);
            if (!isOk(startStatus)) {
                const auto error = coordinator->lastError();
                std::fprintf(stderr,
                             "capture start failed: exchange=%s symbol=%s %s\n",
                             venue.exchange,
                             venue.symbol,
                             !error.empty() ? error.c_str() : statusToString(startStatus).data());
                for (auto& running : coordinators) (void)running->finalizeSession();
                return 1;
            }
            std::printf("capture started: channel=bookticker exchange=%s market=futures_usd symbol=%s duration=%llds dir=%s\n",
                        venue.exchange,
                        venue.symbol,
                        static_cast<long long>(venueConfig.durationSec),
                        venueConfig.outputDir.string().c_str());
            coordinators.push_back(std::move(coordinator));
        }

        std::this_thread::sleep_for(std::chrono::seconds(config.durationSec));
        bool ok = true;
        for (auto& coordinator : coordinators) {
            const auto finalizeStatus = coordinator->finalizeSession();
            if (!isOk(finalizeStatus)) {
                ok = false;
                const auto error = coordinator->lastError();
                std::fprintf(stderr,
                             "capture finalize failed: %s\n",
                             !error.empty() ? error.c_str() : statusToString(finalizeStatus).data());
            }
        }
        if (!ok) return 1;
        std::puts("capture finished");
        return 0;
    }

    if (!applyOptionalSingleVenueArgs(config, argc, argv)) return 2;

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
    Status startStatus = startChannel(coordinator, channel, config);
    if (startStatus == Status::InvalidArgument && coordinator.lastError().empty()) {
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

    std::printf("capture started: channel=%s exchange=%s market=%s symbol=%s duration=%llds dir=%s trades_warmup=%llds\n",
                channel.c_str(),
                config.exchange.c_str(),
                config.market.c_str(),
                config.symbols.empty() ? "" : config.symbols.front().c_str(),
                static_cast<long long>(config.durationSec),
                config.outputDir.string().c_str(),
                static_cast<long long>(config.tradesHistoryWarmupSec));

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
