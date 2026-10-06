#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../../Runtime/src/Capture/Coordinator/CaptureCoordinator.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "Tui/RecorderTuiSymbols.hpp"

namespace hftrec::app {

namespace {

constexpr long kDetailedCandlesMaxLimit = 1'000'000;

void printUsage() {
    std::puts("Explicit history capture: capture <candles|candles2|candles2_bulk|trades_history> [seconds] [output] [exchange] [symbol] [market]");
    std::puts("Live compressed capture: capture --venue exchange/market --symbol CANONICAL [--full-universe] --channels bookticker,trades --duration-sec N --output DIR --max-bytes N");
}

capture::CaptureConfig makeDefaultConfig() {
    capture::CaptureConfig config{};
    config.exchange = "binance";
    config.market = "futures";
    config.symbols = {"ETH_USDT"};
    config.outputDir = recordings::defaultRecordingsRoot();
    config.durationSec = 10;
    config.snapshotIntervalSec = 60;
    config.tradesHistoryWarmupSec = 0;
    config.liveCacheMode = capture::LiveCacheMode::Off;
    return config;
}

void applyTransientRouteSymbol(capture::CaptureConfig& config) {
    config.routeSymbols.clear();
}









bool isDetailedCandlesChannel(std::string_view channel) noexcept {
    return channel == "candles2" || channel == "candle2" || channel == "detailed_candles" ||
           channel == "detailed-candles" || channel == "klines2";
}

bool isDetailedCandlesBulkChannel(std::string_view channel) noexcept {
    return channel == "candles2_bulk" || channel == "candles2-bulk" ||
           channel == "bulk-candles2" || channel == "bulk_candles2" ||
           channel == "klines2_bulk" || channel == "klines2-bulk";
}

bool isTradesHistoryChannel(std::string_view channel) noexcept {
    return channel == "trades_history" || channel == "trade_history" ||
           channel == "historical_trades" || channel == "history_trades";
}

Status startChannel(capture::CaptureCoordinator& coordinator,
                    const std::string& channel,
                    const capture::CaptureConfig& config) {
    if (channel == "candles" || channel == "candle" || channel == "klines") {
        const auto sessionStatus = coordinator.ensureSession(config);
        if (!isOk(sessionStatus)) return sessionStatus;
        return coordinator.captureCandlesOnce(config);
    }
    if (isDetailedCandlesBulkChannel(channel)) {
        return coordinator.captureDetailedCandlesBulk(config);
    }
    if (isDetailedCandlesChannel(channel)) {
        return coordinator.captureDetailedCandlesOnce(config);
    }
    if (isTradesHistoryChannel(channel)) {
        return coordinator.captureTradesHistoryOnce(config);
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
            if (limit < 1 || limit > kDetailedCandlesMaxLimit) {
                std::fputs("capture: --limit must be in [1,1000000]\n", stderr);
                return false;
            }
            config.detailedCandlesLimit = static_cast<std::uint32_t>(limit);
            continue;
        }
        if (arg == "--candles-page-limit") {
            if (i + 1 >= argc) {
                std::fputs("capture: --candles-page-limit requires a value\n", stderr);
                return false;
            }
            const long limit = std::strtol(argv[++i], nullptr, 10);
            if (limit < 0 || limit > kDetailedCandlesMaxLimit) {
                std::fputs("capture: --candles-page-limit must be in [0,1000000]\n", stderr);
                return false;
            }
            config.detailedCandlesPageLimit = static_cast<std::uint32_t>(limit);
            continue;
        }
        if (arg == "--candles-attempts") {
            if (i + 1 >= argc) {
                std::fputs("capture: --candles-attempts requires a value\n", stderr);
                return false;
            }
            const long attempts = std::strtol(argv[++i], nullptr, 10);
            if (attempts < 1 || attempts > 20) {
                std::fputs("capture: --candles-attempts must be in [1,20]\n", stderr);
                return false;
            }
            config.detailedCandlesMaxAttemptsPerPage = static_cast<std::uint32_t>(attempts);
            continue;
        }
        if (arg == "--candles-empty-windows") {
            if (i + 1 >= argc) {
                std::fputs("capture: --candles-empty-windows requires a value\n", stderr);
                return false;
            }
            const long windows = std::strtol(argv[++i], nullptr, 10);
            if (windows < 0 || windows > 1000000) {
                std::fputs("capture: --candles-empty-windows must be in [0,1000000]\n", stderr);
                return false;
            }
            config.detailedCandlesMaxEmptyWindows = static_cast<std::uint32_t>(windows);
            continue;
        }
        if (arg == "--end-ns") {
            if (i + 1 >= argc) {
                std::fputs("capture: --end-ns requires a value\n", stderr);
                return false;
            }
            const long long endNs = std::strtoll(argv[++i], nullptr, 10);
            if (endNs < 0) {
                std::fputs("capture: --end-ns must be >= 0\n", stderr);
                return false;
            }
            config.detailedCandlesEndNs = endNs;
            config.tradesHistoryEndNs = endNs;
            continue;
        }
        if (arg == "--candles-end-ns") {
            if (i + 1 >= argc) {
                std::fputs("capture: --candles-end-ns requires a value\n", stderr);
                return false;
            }
            const long long endNs = std::strtoll(argv[++i], nullptr, 10);
            if (endNs < 0) {
                std::fputs("capture: --candles-end-ns must be >= 0\n", stderr);
                return false;
            }
            config.detailedCandlesEndNs = endNs;
            continue;
        }
        if (arg == "--history-end-ns") {
            if (i + 1 >= argc) {
                std::fputs("capture: --history-end-ns requires a value\n", stderr);
                return false;
            }
            const long long endNs = std::strtoll(argv[++i], nullptr, 10);
            if (endNs < 0) {
                std::fputs("capture: --history-end-ns must be >= 0\n", stderr);
                return false;
            }
            config.tradesHistoryEndNs = endNs;
            continue;
        }
        if (arg == "--history-sec") {
            if (i + 1 >= argc) {
                std::fputs("capture: --history-sec requires a value\n", stderr);
                return false;
            }
            applyTradesWarmupArg(config, argv[++i]);
            continue;
        }
        if (arg == "--history-page-limit") {
            if (i + 1 >= argc) {
                std::fputs("capture: --history-page-limit requires a value\n", stderr);
                return false;
            }
            const long limit = std::strtol(argv[++i], nullptr, 10);
            if (limit < 1 || limit > 1000000) {
                std::fputs("capture: --history-page-limit must be in [1,1000000]\n", stderr);
                return false;
            }
            config.tradesHistoryPageLimit = static_cast<std::uint32_t>(limit);
            continue;
        }
        if (arg == "--history-max-rows") {
            if (i + 1 >= argc) {
                std::fputs("capture: --history-max-rows requires a value\n", stderr);
                return false;
            }
            const long limit = std::strtol(argv[++i], nullptr, 10);
            if (limit < 0 || limit > 1000000) {
                std::fputs("capture: --history-max-rows must be in [0,1000000]\n", stderr);
                return false;
            }
            config.tradesHistoryMaxRows = static_cast<std::uint32_t>(limit);
            continue;
        }
        positional.push_back(argv[i]);
    }
    return true;
}

}  // namespace

int runManagedCapture(int argc,char** argv);

int runCapture(int argc, char** argv) {
    if(argc<2) return runManagedCapture(argc,argv);
    // History is an explicit cold action; normal realtime capture always uses
    // the one Recorder-managed Parser session and compressed binary corpus.
    bool history=false;
    for(int i=1;i<argc;++i) {
        const std::string_view token=argv[i];
        history=history || token=="candles" || token=="candle" || token=="klines" ||
            isDetailedCandlesChannel(token) || isDetailedCandlesBulkChannel(token) || isTradesHistoryChannel(token);
    }
    if(!history) return runManagedCapture(argc,argv);
    auto config=makeDefaultConfig();std::vector<char*> positional;
    if(!stripCaptureOptions(config,argc,argv,positional) || positional.size()<2u) return 2;
    argc=static_cast<int>(positional.size());argv=positional.data();const std::string channel=argv[1];
    if(!(channel=="candles" || channel=="candle" || channel=="klines" || isDetailedCandlesChannel(channel) ||
         isDetailedCandlesBulkChannel(channel) || isTradesHistoryChannel(channel))) {printUsage();return 2;}
    if(argc>=3) config.durationSec=std::strtoll(argv[2],nullptr,10);
    if(argc>=4) config.outputDir=recordings::normalizeExplicitRecordingsPath(argv[3]);
    if(!applyOptionalSingleVenueArgs(config,argc,argv)) return 2;
    applyTransientRouteSymbol(config);capture::CaptureCoordinator coordinator;
    const auto status=startChannel(coordinator,channel,config);
    if(!isOk(status)) {std::fprintf(stderr,"history capture failed: %s\n",coordinator.lastError().c_str());return 1;}
    const auto finalized=coordinator.finalizeSession();
    if(!isOk(finalized)) {std::fprintf(stderr,"history finalize failed: %s\n",coordinator.lastError().c_str());return 1;}
    std::printf("history capture complete: session=%s\n",coordinator.sessionDirCopy().c_str());return 0;
}

}  // namespace hftrec::app
