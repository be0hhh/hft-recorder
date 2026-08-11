#include "core/capture/ParserMarketCaptureClient.hpp"
#include "core/corpus/BinaryMarketCorpusWriter.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace hftrec::app {
namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t gParserCaptureInterrupted = 0;

void handleParserCaptureSignal(int) noexcept {
    gParserCaptureInterrupted = 1;
}

struct ParserCaptureOptions final {
    std::filesystem::path runtimeDirectory{};
    std::filesystem::path output{};
    std::uint64_t durationSec{0u};
    std::uint64_t maximumBytes{0u};
    std::uint64_t segmentBytes{64u * 1024u * 1024u};
    bool terminalDashboard{false};
};

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder parser-capture catalog --runtime-dir PATH");
    std::puts("  hft-recorder parser-capture doctor --runtime-dir PATH");
    std::puts("  hft-recorder parser-capture capture --runtime-dir PATH --output DIR --duration-sec N --max-bytes N [--segment-bytes N]");
    std::puts("  hft-recorder parser-capture tui --runtime-dir PATH --output DIR --duration-sec N --max-bytes N [--segment-bytes N]");
    std::puts("");
    std::puts("parserd must already be running with the same owner and runtime directory.");
    std::puts("Capture always includes every source and every channel exposed by parserd.");
}

[[nodiscard]] bool parsePositive(std::string_view text,
                                 std::uint64_t& output) noexcept {
    output = 0u;
    if (text.empty()) return false;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), output, 10);
    return parsed.ec == std::errc{} &&
        parsed.ptr == text.data() + text.size() && output != 0u;
}

[[nodiscard]] bool parseOptions(int argc,
                                char** argv,
                                int begin,
                                bool capture,
                                ParserCaptureOptions& options) {
    for (int index = begin; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto requireValue = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                std::fprintf(stderr,
                             "parser-capture: %s requires a value\n", name);
                return nullptr;
            }
            return argv[++index];
        };
        if (argument == "--runtime-dir") {
            const char* value = requireValue("--runtime-dir");
            if (!value) return false;
            options.runtimeDirectory = value;
        } else if (argument == "--output") {
            const char* value = requireValue("--output");
            if (!value) return false;
            options.output = value;
        } else if (argument == "--duration-sec") {
            const char* value = requireValue("--duration-sec");
            if (!value || !parsePositive(value, options.durationSec)) {
                std::fputs("parser-capture: --duration-sec must be a positive integer\n",
                           stderr);
                return false;
            }
        } else if (argument == "--max-bytes") {
            const char* value = requireValue("--max-bytes");
            if (!value || !parsePositive(value, options.maximumBytes)) {
                std::fputs("parser-capture: --max-bytes must be a positive integer\n",
                           stderr);
                return false;
            }
        } else if (argument == "--segment-bytes") {
            const char* value = requireValue("--segment-bytes");
            if (!value || !parsePositive(value, options.segmentBytes)) {
                std::fputs("parser-capture: --segment-bytes must be a positive integer\n",
                           stderr);
                return false;
            }
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            return false;
        } else {
            std::fprintf(stderr,
                         "parser-capture: unknown option '%.*s'\n",
                         static_cast<int>(argument.size()), argument.data());
            return false;
        }
    }
    if (options.runtimeDirectory.empty()) {
        std::fputs("parser-capture: --runtime-dir is required\n", stderr);
        return false;
    }
    if (capture && (options.output.empty() || options.durationSec == 0u ||
                    options.maximumBytes == 0u)) {
        std::fputs("parser-capture: --output, --duration-sec, and --max-bytes are required\n",
                   stderr);
        return false;
    }
    if (capture && options.durationSec >
            std::numeric_limits<std::uint64_t>::max() / 1'000'000'000u) {
        std::fputs("parser-capture: duration is too large\n", stderr);
        return false;
    }
    return true;
}

[[nodiscard]] const char* channelName(std::size_t index) noexcept {
    static constexpr std::array<const char*, corpus::kBinaryMarketChannelCount>
        names{"bbo", "trade", "depth", "liquidation", "mark_price",
              "index_price", "funding", "price_limit"};
    return index < names.size() ? names[index] : "unknown";
}

[[nodiscard]] std::string channelList(std::uint16_t mask) {
    std::string result;
    for (std::size_t index = 0u;
         index < corpus::kBinaryMarketChannelCount; ++index) {
        if ((mask & (std::uint16_t{1u} << index)) == 0u) continue;
        if (!result.empty()) result += ',';
        result += channelName(index);
    }
    return result.empty() ? "none" : result;
}

[[nodiscard]] std::string fixedText(const char* data,
                                    std::uint8_t bytes) {
    return data && bytes != 0u ? std::string(data, data + bytes)
                                : std::string{};
}

void printCatalog(const std::vector<corpus::BinaryMarketSource>& sources,
                  const capture::ParserMarketCaptureSnapshot& snapshot) {
    std::printf("parser-capture catalog: producer_epoch=%llu directory_generation=%llu sources=%zu shards=%u\n",
                static_cast<unsigned long long>(snapshot.producerEpoch),
                static_cast<unsigned long long>(snapshot.directoryGeneration),
                sources.size(), static_cast<unsigned>(snapshot.shardCount));
    for (const auto& source : sources) {
        const std::string venue = fixedText(source.venue.data(),
                                            source.venueBytes);
        const std::string market = fixedText(source.market.data(),
                                             source.marketBytes);
        const std::string symbol = fixedText(source.canonicalSymbol.data(),
                                             source.canonicalSymbolBytes);
        const std::string available = channelList(source.availableChannelMask);
        const std::string replay = channelList(source.traderReplayChannelMask);
        std::printf("  source=%u %s/%s/%s scale=%u:%u available=%s trader_replay=%s\n",
                    source.sourceId, venue.c_str(), market.c_str(),
                    symbol.c_str(), static_cast<unsigned>(source.priceScale),
                    static_cast<unsigned>(source.quantityScale),
                    available.c_str(), replay.c_str());
    }
}

[[nodiscard]] std::int64_t realtimeNowNs() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto value =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    return value > 0 ? value : 0;
}

[[nodiscard]] std::uint64_t monotonicNowNs() noexcept {
    timespec value{};
    if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 ||
        value.tv_nsec < 0) return 0u;
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000u +
        static_cast<std::uint64_t>(value.tv_nsec);
}

void renderDashboard(
    const ParserCaptureOptions& options,
    const capture::ParserMarketCaptureSnapshot& client,
    const corpus::BinaryMarketWriterSnapshot& writer,
    std::size_t sourceCount,
    std::chrono::seconds remaining) {
    std::fputs("\033[2J\033[H", stdout);
    std::puts("hft-recorder / parser market capture");
    std::printf("parserd: %s  producer_epoch=%llu  sources=%zu  shards=%u\n",
                client.producerDisconnected ? "DISCONNECTED" : "CONNECTED",
                static_cast<unsigned long long>(client.producerEpoch),
                sourceCount, static_cast<unsigned>(client.shardCount));
    std::printf("records=%llu pending=%llu gaps=%llu loss_epoch=%llu remaining=%llds\n",
                static_cast<unsigned long long>(writer.recordCount),
                static_cast<unsigned long long>(client.pendingRecords),
                static_cast<unsigned long long>(writer.gapCount),
                static_cast<unsigned long long>(client.lossEpoch),
                static_cast<long long>(remaining.count()));
    std::printf("budget=%llu/%llu bytes  output=%s\n",
                static_cast<unsigned long long>(writer.projectedBytes),
                static_cast<unsigned long long>(options.maximumBytes),
                options.output.string().c_str());
    std::puts("Ctrl-C: freeze parser capture, drain committed records, seal corpus");
    std::fflush(stdout);
}

[[nodiscard]] int runCatalogOrDoctor(
    const ParserCaptureOptions& options,
    bool doctor) {
    capture::ParserMarketCaptureClient client{};
    std::string error;
    Status status = client.connect(options.runtimeDirectory, error);
    if (!isOk(status)) {
        std::fprintf(stderr, "parser-capture: %s (%s)\n", error.c_str(),
                     statusToString(status).data());
        return 1;
    }
    std::vector<corpus::BinaryMarketSource> sources;
    status = client.loadSourceDirectory(sources, error);
    if (!isOk(status)) {
        std::fprintf(stderr, "parser-capture: %s (%s)\n", error.c_str(),
                     statusToString(status).data());
        return 1;
    }
    printCatalog(sources, client.snapshot());
    status = client.stop(error);
    if (!isOk(status)) {
        std::fprintf(stderr, "parser-capture: %s (%s)\n", error.c_str(),
                     statusToString(status).data());
        return 1;
    }
    if (doctor) {
        const auto snapshot = client.snapshot();
        std::printf("parser-capture doctor: OK protocol=ready arena=validated directory=stable loss_epoch=%llu\n",
                    static_cast<unsigned long long>(snapshot.lossEpoch));
    }
    return 0;
}

[[nodiscard]] int runCaptureSession(const ParserCaptureOptions& options) {
    capture::ParserMarketCaptureClient client{};
    std::string error;
    Status status = client.connect(options.runtimeDirectory, error);
    if (!isOk(status)) {
        std::fprintf(stderr, "parser-capture connect failed: %s (%s)\n",
                     error.c_str(), statusToString(status).data());
        return 1;
    }
    std::vector<corpus::BinaryMarketSource> sources;
    status = client.loadSourceDirectory(sources, error);
    if (!isOk(status)) {
        std::fprintf(stderr, "parser-capture directory failed: %s (%s)\n",
                     error.c_str(), statusToString(status).data());
        return 1;
    }
    const auto attached = client.snapshot();
    const std::uint64_t durationNs =
        options.durationSec * 1'000'000'000u;
    if (attached.captureStartedReceiveNs <= 0 ||
        attached.captureStartedMonotonicNs == 0u ||
        durationNs > std::numeric_limits<std::uint64_t>::max() -
            attached.captureStartedMonotonicNs) {
        std::fputs("parser-capture: capture activation clock is invalid\n",
                   stderr);
        return 1;
    }
    corpus::BinaryMarketCorpusWriter writer{};
    status = writer.start({
        .root = options.output,
        .sources = sources,
        .producerEpoch = attached.producerEpoch,
        .maximumBytes = options.maximumBytes,
        .segmentTargetBytes = options.segmentBytes,
        .targetDurationNs = durationNs,
        .startedReceiveNs = attached.captureStartedReceiveNs,
        .startedMonotonicNs = attached.captureStartedMonotonicNs,
        .ringCapacity = attached.ringCapacity,
        .shardCount = attached.shardCount});
    if (!isOk(status)) {
        std::fprintf(stderr,
                     "parser-capture writer start failed: %s; output must be absent/empty and max-bytes must cover metadata plus the bounded final ring drain\n",
                     statusToString(status).data());
        return 1;
    }

    const auto previousInt = std::signal(SIGINT, handleParserCaptureSignal);
    const auto previousTerm = std::signal(SIGTERM, handleParserCaptureSignal);
    gParserCaptureInterrupted = 0;
    const std::uint64_t deadlineMonotonicNs =
        attached.captureStartedMonotonicNs + durationNs;
    auto nextDashboard = Clock::now();
    corpus::BinaryMarketStopReason stopReason =
        corpus::BinaryMarketStopReason::Duration;
    bool failed = false;
    bool frozen = false;

    while (true) {
        const auto now = Clock::now();
        const std::uint64_t nowMonotonicNs = monotonicNowNs();
        if (nowMonotonicNs == 0u) {
            stopReason = corpus::BinaryMarketStopReason::Error;
            failed = true;
            std::fputs("parser-capture: monotonic clock unavailable\n",
                       stderr);
            break;
        }
        if (gParserCaptureInterrupted != 0) {
            stopReason = corpus::BinaryMarketStopReason::Requested;
            break;
        }
        if (nowMonotonicNs >= deadlineMonotonicNs) {
            stopReason = corpus::BinaryMarketStopReason::Duration;
            break;
        }
        if (client.producerDisconnected()) {
            stopReason = corpus::BinaryMarketStopReason::ParserDisconnected;
            const Status freezeStatus = client.freezeDisconnected(error);
            if (isOk(freezeStatus)) {
                frozen = true;
            } else {
                failed = true;
                stopReason = corpus::BinaryMarketStopReason::Error;
                std::fprintf(stderr,
                             "parser-capture disconnect freeze failed: %s (%s)\n",
                             error.c_str(), statusToString(freezeStatus).data());
            }
            break;
        }
        std::uint64_t drained = 0u;
        status = client.drain(writer, 0u, drained, error);
        if (status == Status::OutOfRange) {
            stopReason = corpus::BinaryMarketStopReason::Quota;
            break;
        }
        if (!isOk(status)) {
            stopReason = corpus::BinaryMarketStopReason::Error;
            failed = true;
            std::fprintf(stderr, "parser-capture drain failed: %s (%s)\n",
                         error.c_str(), statusToString(status).data());
            break;
        }
        if (options.terminalDashboard && ::isatty(STDOUT_FILENO) == 1 &&
            now >= nextDashboard) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::seconds>(std::chrono::nanoseconds(
                    deadlineMonotonicNs - nowMonotonicNs));
            renderDashboard(options, client.snapshot(), writer.snapshot(),
                            sources.size(), remaining);
            nextDashboard = now + std::chrono::milliseconds(250);
        }
        if (drained == 0u) std::this_thread::yield();
    }

    if (!frozen) {
        const Status stopStatus = client.stop(error);
        if (isOk(stopStatus)) {
            frozen = true;
        } else if (client.producerDisconnected()) {
            const Status freezeStatus = client.freezeDisconnected(error);
            if (isOk(freezeStatus)) {
                frozen = true;
                stopReason =
                    corpus::BinaryMarketStopReason::ParserDisconnected;
            } else {
                failed = true;
                stopReason = corpus::BinaryMarketStopReason::Error;
                std::fprintf(stderr,
                             "parser-capture disconnect freeze failed: %s (%s)\n",
                             error.c_str(), statusToString(freezeStatus).data());
            }
        } else {
            failed = true;
            stopReason = corpus::BinaryMarketStopReason::Error;
            std::fprintf(stderr, "parser-capture stop failed: %s (%s)\n",
                         error.c_str(), statusToString(stopStatus).data());
        }
    }

    if (frozen) {
        const Status drainModeStatus = writer.beginFinalDrain();
        if (!isOk(drainModeStatus)) {
            failed = true;
            stopReason = corpus::BinaryMarketStopReason::Error;
            std::fprintf(stderr,
                         "parser-capture final drain budget activation failed: %s\n",
                         statusToString(drainModeStatus).data());
        }
    }
    if (frozen && !failed) {
        while (client.snapshot().pendingRecords != 0u) {
            std::uint64_t drained = 0u;
            status = client.drain(writer, 0u, drained, error);
            if (status == Status::OutOfRange) {
                failed = true;
                stopReason = corpus::BinaryMarketStopReason::Error;
                std::fputs(
                    "parser-capture: bounded final drain reserve was exhausted\n",
                    stderr);
                break;
            }
            if (!isOk(status) || drained == 0u) {
                failed = true;
                stopReason = corpus::BinaryMarketStopReason::Error;
                std::fprintf(stderr,
                             "parser-capture final drain failed: %s (%s)\n",
                             error.c_str(), statusToString(status).data());
                break;
            }
        }
    }
    if (frozen) {
        const Status gapStatus = client.appendFrozenLosses(writer, error);
        if (!isOk(gapStatus)) {
            failed = true;
            stopReason = corpus::BinaryMarketStopReason::Error;
            std::fprintf(stderr,
                         "parser-capture loss ledger failed: %s (%s)\n",
                         error.c_str(), statusToString(gapStatus).data());
        }
    }

    const std::int64_t finalizedReceiveNs = realtimeNowNs();
    const Status finalizeStatus = writer.finalize(
        stopReason, finalizedReceiveNs > 0 ? finalizedReceiveNs
                                           : attached.captureStartedReceiveNs);
    if (!isOk(finalizeStatus)) {
        failed = true;
        std::fprintf(stderr, "parser-capture finalize failed: %s\n",
                     statusToString(finalizeStatus).data());
    }
    std::signal(SIGINT, previousInt);
    std::signal(SIGTERM, previousTerm);
    if (options.terminalDashboard && ::isatty(STDOUT_FILENO) == 1)
        std::putchar('\n');
    const auto clientSnapshot = client.snapshot();
    const auto writerSnapshot = writer.snapshot();
    std::printf("parser-capture finished: reason=%u records=%llu gaps=%llu projected_bytes=%llu output=%s\n",
                static_cast<unsigned>(stopReason),
                static_cast<unsigned long long>(writerSnapshot.recordCount),
                static_cast<unsigned long long>(writerSnapshot.gapCount),
                static_cast<unsigned long long>(writerSnapshot.projectedBytes),
                options.output.string().c_str());
    if (clientSnapshot.lossEpoch != 0u) {
        std::fputs("parser-capture: corpus contains isolated source/channel gaps and affected backtest intervals will fail closed\n",
                   stderr);
    }
    return failed ? 1 : 0;
}

}  // namespace

int runParserCapture(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        printUsage();
        return 0;
    }
    const std::string_view command{argv[1]};
    const bool capture = command == "capture" || command == "tui";
    const bool catalog = command == "catalog" || command == "doctor";
    if (!capture && !catalog) {
        std::fprintf(stderr, "parser-capture: unknown command '%.*s'\n",
                     static_cast<int>(command.size()), command.data());
        printUsage();
        return 2;
    }
    ParserCaptureOptions options{};
    options.terminalDashboard = command == "tui";
    if (!parseOptions(argc, argv, 2, capture, options)) return 2;
    return capture ? runCaptureSession(options)
                   : runCatalogOrDoctor(options, command == "doctor");
}

}  // namespace hftrec::app
