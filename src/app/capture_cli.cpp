#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::app {

namespace {

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder capture [--env path] [--api-slot n] [--timeframe tf] [--limit n] [--underlying-symbol symbol] <trades|liquidations|bookticker|orderbook|mark_price|index_price|funding|price_limit|candles|candles2> [seconds] [output_dir] [exchange] [symbol] [market] [trades_warmup_sec]");
    std::puts("  hft-recorder capture [--env path] [--api-slot n] bookticker all [seconds] [output_dir]");
    std::puts("  Current scope: canonical JSON corpus output, one session folder per exchange/symbol.");
    std::puts("");
    std::puts("Examples:");
    std::puts("  hft-recorder capture bookticker all 60 ./recordings");
    std::puts("  hft-recorder capture bookticker 10 ./recordings binance BTCUSDT");
    std::puts("  hft-recorder capture bookticker 10 ./recordings bybit BTCUSDT futures");
    std::puts("  hft-recorder capture bookticker 10 ./recordings kucoin BTCUSDTM");
    std::puts("  hft-recorder capture bookticker 10 ./recordings gate BTC_USDT");
    std::puts("  hft-recorder capture bookticker 10 ./recordings aster ASTERUSDT spot");
    std::puts("  hft-recorder capture bookticker 10 ./recordings gate BTC_USDT margin");
    std::puts("  hft-recorder capture bookticker 10 ./recordings okx BTC-USDT-SWAP futures");
    std::puts("  hft-recorder capture mark_price 30 ./recordings binance BTCUSDT futures");
    std::puts("  hft-recorder capture index_price 30 ./recordings bybit BTCUSDT futures");
    std::puts("  hft-recorder capture funding 30 ./recordings gate BTC_USDT futures");
    std::puts("  hft-recorder capture price_limit 30 ./recordings bitget BTCUSDT futures");
    std::puts("  hft-recorder capture trades 30 ./recordings binance ETHUSDT futures 300");
    std::puts("  hft-recorder capture --env ./.env --api-slot 1 trades 30 ./recordings binance ETHUSDT futures 300");
    std::puts("  hft-recorder capture candles 1 ./recordings binance BSBUSDT");
    std::puts("  hft-recorder capture --env ./.env --api-slot 1 --timeframe 10m --limit 5000 --underlying-symbol SBER candles2 1 ./recordings moex SRM6 futures");
}

capture::CaptureConfig makeDefaultConfig() {
    capture::CaptureConfig config{};
    config.exchange = "binance";
    config.market = "futures";
    config.symbols = {"ETHUSDT"};
    config.outputDir = "./recordings";
    config.durationSec = 10;
    config.snapshotIntervalSec = 60;
    return config;
}

struct VenueDefault {
    const char* exchange;
    const char* market;
    const char* symbol;
};

constexpr VenueDefault kBookTickerVenueDefaults[] = {
    {"binance", "futures", "BTCUSDT"},
    {"bybit", "futures", "BTCUSDT"},
    {"kucoin", "futures", "BTCUSDTM"},
    {"gate", "futures", "BTC_USDT"},
    {"bitget", "futures", "BTCUSDT"},
    {"aster", "futures", "BTCUSDT"},
    {"aster", "spot", "ASTERUSDT"},
    {"okx", "futures", "BTC-USDT-SWAP"},
};

bool isMarkPriceChannel(std::string_view channel) noexcept {
    return channel == "mark_price" || channel == "mark-price" || channel == "markprice" || channel == "mark";
}

bool isIndexPriceChannel(std::string_view channel) noexcept {
    return channel == "index_price" || channel == "index-price" || channel == "indexprice" || channel == "index";
}

bool isFundingChannel(std::string_view channel) noexcept {
    return channel == "funding" || channel == "funding_rate" || channel == "funding-rate" || channel == "fundingrate";
}

bool isPriceLimitChannel(std::string_view channel) noexcept {
    return channel == "price_limit" || channel == "price-limit" || channel == "pricelimit" || channel == "limit" || channel == "limits";
}

bool isDetailedCandlesChannel(std::string_view channel) noexcept {
    return channel == "candles2" || channel == "candle2" || channel == "detailed_candles" ||
           channel == "detailed-candles" || channel == "klines2";
}

bool isMoexExchange(std::string_view exchange) noexcept {
    return exchange == "moex" || exchange == "MOEX";
}

capture::CaptureConfig normalizeDetailedCandlesConfig(capture::CaptureConfig config) {
    if (isMoexExchange(config.exchange) && config.detailedCandlesTimeframe == "15m") {
        config.detailedCandlesTimeframe = "10m";
    }
    return config;
}

Status startChannel(capture::CaptureCoordinator& coordinator,
                    const std::string& channel,
                    const capture::CaptureConfig& config) {
    if (channel == "trades") return coordinator.startTrades(config);
    if (channel == "liquidations" || channel == "liquidation" || channel == "forceOrder") return coordinator.startLiquidations(config);
    if (channel == "bookticker") return coordinator.startBookTicker(config);
    if (channel == "orderbook") return coordinator.startOrderbook(config);
    if (isMarkPriceChannel(channel)) return coordinator.startMarkPrice(config);
    if (isIndexPriceChannel(channel)) return coordinator.startIndexPrice(config);
    if (isFundingChannel(channel)) return coordinator.startFunding(config);
    if (isPriceLimitChannel(channel)) return coordinator.startPriceLimit(config);
    if (channel == "candles" || channel == "candle" || channel == "klines") {
        const auto sessionStatus = coordinator.ensureSession(config);
        if (!isOk(sessionStatus)) return sessionStatus;
        return coordinator.captureCandlesOnce(config);
    }
    if (isDetailedCandlesChannel(channel)) {
        return coordinator.captureDetailedCandlesOnce(normalizeDetailedCandlesConfig(config));
    }
    return Status::InvalidArgument;
}

bool isMarketText(const std::string& text) noexcept {
    return text == "futures" || text == "futures_usd" || text == "spot" || text == "shares" || text == "margin" ||
           text == "inverse" || text == "swap";
}

void applyTradesWarmupArg(capture::CaptureConfig& config, const char* text) noexcept {
    config.tradesHistoryWarmupSec = std::strtoll(text, nullptr, 10);
    if (config.tradesHistoryWarmupSec < 0) config.tradesHistoryWarmupSec = 0;
    if (config.tradesHistoryWarmupSec > 86400) config.tradesHistoryWarmupSec = 86400;
}

bool applyOptionalSingleVenueArgs(capture::CaptureConfig& config, int argc, char** argv) {
    if (argc >= 5) {
        config.exchange = argv[4];
    }
    if (argc >= 6) {
        config.symbols = {argv[5]};
    }
    if (argc >= 7) {
        const std::string marketOrWarmup = argv[6];
        if (isMarketText(marketOrWarmup)) {
            config.market = marketOrWarmup;
            if (argc >= 8) applyTradesWarmupArg(config, argv[7]);
        } else {
            applyTradesWarmupArg(config, argv[6]);
        }
    }
    return true;
}

bool stripCaptureOptions(capture::CaptureConfig& config,
                         int argc,
                         char** argv,
                         std::vector<char*>& positional) {
    positional.clear();
    positional.reserve(static_cast<std::size_t>(argc));
    if (argc > 0) positional.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--env") {
            if (i + 1 >= argc) {
                std::fputs("capture: --env requires a path\n", stderr);
                return false;
            }
            config.envPath = argv[++i];
            continue;
        }
        if (arg == "--api-slot") {
            if (i + 1 >= argc) {
                std::fputs("capture: --api-slot requires a value\n", stderr);
                return false;
            }
            const long slot = std::strtol(argv[++i], nullptr, 10);
            if (slot < 1 || slot > 255) {
                std::fputs("capture: --api-slot must be in [1,255]\n", stderr);
                return false;
            }
            config.apiSlot = static_cast<std::uint8_t>(slot);
            continue;
        }
        if (arg == "--timeframe") {
            if (i + 1 >= argc) {
                std::fputs("capture: --timeframe requires a value\n", stderr);
                return false;
            }
            config.detailedCandlesTimeframe = argv[++i];
            continue;
        }
        if (arg == "--limit") {
            if (i + 1 >= argc) {
                std::fputs("capture: --limit requires a value\n", stderr);
                return false;
            }
            const long limit = std::strtol(argv[++i], nullptr, 10);
            if (limit < 1 || limit > 5000) {
                std::fputs("capture: --limit must be in [1,5000]\n", stderr);
                return false;
            }
            config.detailedCandlesLimit = static_cast<std::uint32_t>(limit);
            continue;
        }
        if (arg == "--underlying-symbol") {
            if (i + 1 >= argc) {
                std::fputs("capture: --underlying-symbol requires a value\n", stderr);
                return false;
            }
            config.detailedCandlesUnderlyingSymbolHint = argv[++i];
            continue;
        }
        positional.push_back(argv[i]);
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
    std::vector<char*> positional;
    if (!stripCaptureOptions(config, argc, argv, positional)) return 2;
    argc = static_cast<int>(positional.size());
    argv = positional.data();
    if (argc < 2) {
        printUsage();
        return 2;
    }

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
            venueConfig.market = venue.market;
            venueConfig.symbols = {venue.symbol};

            auto coordinator = std::make_unique<capture::CaptureCoordinator>();
            const auto startStatus = coordinator->startBookTicker(venueConfig);
            if (!isOk(startStatus)) {
                const auto error = coordinator->lastError();
                std::fprintf(stderr,
                             "capture start failed: exchange=%s market=%s symbol=%s %s\n",
                             venue.exchange,
                             venue.market,
                             venue.symbol,
                             !error.empty() ? error.c_str() : statusToString(startStatus).data());
                for (auto& running : coordinators) (void)running->finalizeSession();
                return 1;
            }
            std::printf("capture started: channel=bookticker exchange=%s market=%s symbol=%s duration=%llds dir=%s env=%s api_slot=%u\n",
                        venue.exchange,
                        venue.market,
                        venue.symbol,
                        static_cast<long long>(venueConfig.durationSec),
                        venueConfig.outputDir.string().c_str(),
                        venueConfig.envPath.string().c_str(),
                        static_cast<unsigned>(venueConfig.apiSlot == 0u ? 1u : venueConfig.apiSlot));
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
    if (isDetailedCandlesChannel(channel)) {
        config = normalizeDetailedCandlesConfig(std::move(config));
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

    if (isDetailedCandlesChannel(channel)) {
        std::printf("capture finished: channel=%s exchange=%s market=%s symbol=%s dir=%s env=%s api_slot=%u timeframe=%s limit=%u candles2_rows=%llu session=%s\n",
                    channel.c_str(),
                    config.exchange.c_str(),
                    config.market.c_str(),
                    config.symbols.empty() ? "" : config.symbols.front().c_str(),
                    config.outputDir.string().c_str(),
                    config.envPath.string().c_str(),
                    static_cast<unsigned>(config.apiSlot == 0u ? 1u : config.apiSlot),
                    config.detailedCandlesTimeframe.c_str(),
                    static_cast<unsigned>(config.detailedCandlesLimit),
                    static_cast<unsigned long long>(coordinator.candles2Count()),
                    coordinator.sessionDirCopy().string().c_str());
        return 0;
    }

    std::printf("capture started: channel=%s exchange=%s market=%s symbol=%s duration=%llds dir=%s env=%s api_slot=%u trades_warmup=%llds timeframe=%s limit=%u\n",
                channel.c_str(),
                config.exchange.c_str(),
                config.market.c_str(),
                config.symbols.empty() ? "" : config.symbols.front().c_str(),
                static_cast<long long>(config.durationSec),
                config.outputDir.string().c_str(),
                config.envPath.string().c_str(),
                static_cast<unsigned>(config.apiSlot == 0u ? 1u : config.apiSlot),
                static_cast<long long>(config.tradesHistoryWarmupSec),
                config.detailedCandlesTimeframe.c_str(),
                static_cast<unsigned>(config.detailedCandlesLimit));

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
