#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/capture/CaptureChannelSupport.hpp"
#include "core/capture/CaptureCoordinator.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/tui/RecorderTuiLaunch.hpp"
#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::app {

namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t gPresetRunnerStop = 0;
constexpr std::int64_t kStartSlotGraceSec = 15;
constexpr auto kDeadZeroRowSessionTtl = std::chrono::minutes(5);
constexpr auto kStatusFileInterval = std::chrono::seconds(1);

void handlePresetRunnerSignal(int) {
    gPresetRunnerStop = 1;
}

std::string lowerAscii(std::string_view text) {
    std::string out{text};
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

capture::CaptureConfig makeCaptureConfig(const tui::RecorderTuiJob& job,
                                         const std::filesystem::path& outputDir) {
    capture::CaptureConfig config{};
    config.exchange = job.exchange;
    config.market = job.market;
    config.symbols = {job.symbol};
    config.outputDir = outputDir;
    config.durationSec = job.durationMin > 0 ? job.durationMin * 60 : 0;
    config.snapshotIntervalSec = 60;
    config.tradesHistoryWarmupSec = 0;
    config.tradesHistoryMaxRows = 0u;
    config.liveCacheMode = capture::LiveCacheMode::Off;
    return config;
}

std::int64_t wallNowNs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

std::filesystem::path uniquePath(const std::filesystem::path& parent, const std::string& baseName) {
    std::filesystem::path candidate = parent / baseName;
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int i = 2; i < 1000; ++i) {
        std::ostringstream suffix;
        suffix << baseName << '_' << (i < 10 ? "0" : "") << i;
        candidate = parent / suffix.str();
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return parent / (baseName + "_overflow");
}

struct RunOutputGroups {
    std::filesystem::path root;
    std::int64_t timestampNs{0};
    std::map<std::string, std::filesystem::path> bySymbol;
};

RunOutputGroups makeRunOutputGroups(const tui::RecorderTuiPreset& preset) {
    return RunOutputGroups{.root = preset.outputDir, .timestampNs = wallNowNs(), .bySymbol = {}};
}

std::filesystem::path outputDirForRunJob(RunOutputGroups& groups, const tui::RecorderTuiJob& job) {
    std::string normalizedSymbol = recordings::normalizeRecordingSymbol(job.symbol);
    if (normalizedSymbol.empty()) normalizedSymbol = "UNKNOWN";
    const auto [it, inserted] = groups.bySymbol.emplace(normalizedSymbol, std::filesystem::path{});
    if (inserted) {
        const std::string groupName = recordings::recordingGroupFolderName(groups.timestampNs, normalizedSymbol);
        it->second = uniquePath(groups.root, groupName);
    }
    return it->second;
}

struct RunningJob {
    tui::RecorderTuiJob job{};
    capture::CaptureConfig config{};
    std::unique_ptr<capture::CaptureCoordinator> coordinator{};
    std::future<std::shared_ptr<RunningJob>> startFuture{};
    Clock::time_point scheduledStart{};
    Clock::time_point started{};
    bool startInProgress{false};
    bool launched{false};
    bool running{false};
    bool stopRequested{false};
    bool finalized{false};
    std::string status{"pending"};
    std::string error{};
};

bool anyRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesRunning() || coordinator.liquidationsRunning() || coordinator.bookTickerRunning() ||
           coordinator.orderbookRunning() || coordinator.markPriceRunning() || coordinator.indexPriceRunning() ||
           coordinator.fundingRunning() || coordinator.priceLimitRunning();
}

std::uint64_t totalRows(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesCount() + coordinator.liquidationsCount() + coordinator.bookTickerCount() +
           coordinator.depthCount() + coordinator.markPriceCount() + coordinator.indexPriceCount() +
           coordinator.fundingCount() + coordinator.priceLimitCount();
}

std::int64_t startAgeSeconds(const RunningJob& job, Clock::time_point now) noexcept {
    if (!job.startInProgress) return 0;
    return std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count());
}

bool startStalled(const RunningJob& job, Clock::time_point now) noexcept {
    return job.startInProgress && startAgeSeconds(job, now) >= kStartSlotGraceSec;
}

void appendStartError(RunningJob& job, std::string_view channel, Status status) {
    const std::string last = job.coordinator ? job.coordinator->lastError() : std::string{};
    if (!job.error.empty()) job.error += " | ";
    job.error += std::string(channel) + ": ";
    job.error += last.empty() ? std::string(statusToString(status)) : last;
}

void tryStartChannel(RunningJob& job, std::string_view name, Status status) {
    if (!isOk(status)) appendStartError(job, name, status);
}

RunningJob startJobFromConfig(const tui::RecorderTuiJob& source, capture::CaptureConfig config) {
    RunningJob job{};
    job.job = source;
    job.config = std::move(config);
    job.coordinator = std::make_unique<capture::CaptureCoordinator>();
    job.started = Clock::now();
    job.scheduledStart = job.started;
    job.launched = true;
    job.status = "starting";

    if (source.channels.trades) tryStartChannel(job, "trades", job.coordinator->startTrades(job.config));
    if (source.channels.liquidations) tryStartChannel(job, "liquidations", job.coordinator->startLiquidations(job.config));
    if (source.channels.bookTicker) tryStartChannel(job, "bookticker", job.coordinator->startBookTicker(job.config));
    if (source.channels.orderbook) tryStartChannel(job, "orderbook", job.coordinator->startOrderbook(job.config));
    if (source.channels.markPrice) tryStartChannel(job, "mark_price", job.coordinator->startMarkPrice(job.config));
    if (source.channels.indexPrice) tryStartChannel(job, "index_price", job.coordinator->startIndexPrice(job.config));
    if (source.channels.funding) tryStartChannel(job, "funding", job.coordinator->startFunding(job.config));
    if (source.channels.priceLimit) tryStartChannel(job, "price_limit", job.coordinator->startPriceLimit(job.config));

    job.running = anyRunningChannel(*job.coordinator);
    job.status = job.running ? "running" : "error";
    return job;
}

void requestStopJob(RunningJob& job) {
    if (job.finalized || job.stopRequested) return;
    if (job.startInProgress) {
        job.stopRequested = true;
        job.status = "stopping";
        return;
    }
    if (!job.coordinator) {
        job.stopRequested = true;
        job.running = false;
        job.finalized = true;
        job.status = "stopped";
        return;
    }
    if (job.job.channels.trades) (void)job.coordinator->requestStopTrades();
    if (job.job.channels.liquidations) (void)job.coordinator->requestStopLiquidations();
    if (job.job.channels.bookTicker) (void)job.coordinator->requestStopBookTicker();
    if (job.job.channels.orderbook) (void)job.coordinator->requestStopOrderbook();
    if (job.job.channels.markPrice) (void)job.coordinator->requestStopMarkPrice();
    if (job.job.channels.indexPrice) (void)job.coordinator->requestStopIndexPrice();
    if (job.job.channels.funding) (void)job.coordinator->requestStopFunding();
    if (job.job.channels.priceLimit) (void)job.coordinator->requestStopPriceLimit();
    job.stopRequested = true;
    job.running = false;
    job.status = "stopping";
}

void finalizeJob(RunningJob& job) {
    if (job.finalized) return;
    if (!job.coordinator) {
        job.running = false;
        job.finalized = true;
        if (job.status == "pending") job.status = job.stopRequested ? "stopped" : "skipped";
        return;
    }
    requestStopJob(job);
    const auto status = job.coordinator->finalizeSession();
    if (!isOk(status)) {
        const auto error = job.coordinator->lastError();
        job.error = error.empty() ? std::string(statusToString(status)) : error;
        job.status = "error";
    } else if (!job.error.empty()) {
        job.status = "done_warn";
    } else {
        job.status = "done";
    }
    job.running = false;
    job.finalized = true;
}

bool deadZeroRowJobExpired(const RunningJob& job, Clock::time_point now) noexcept {
    if (!job.launched || job.finalized || job.startInProgress || !job.coordinator) return false;
    if (totalRows(*job.coordinator) != 0u) return false;
    return now - job.started >= kDeadZeroRowSessionTtl;
}

bool cullDeadZeroRowJob(RunningJob& job, Clock::time_point now) {
    if (!deadZeroRowJobExpired(job, now)) return false;
    job.error = "dead session: no rows for 5m";
    finalizeJob(job);
    job.status = "dead";
    job.running = false;
    job.finalized = true;
    return true;
}

std::shared_ptr<RunningJob> startJobWorker(tui::RecorderTuiJob source, capture::CaptureConfig config) {
    return std::make_shared<RunningJob>(startJobFromConfig(source, std::move(config)));
}

void startJobAsync(RunningJob& job) {
    tui::RecorderTuiJob source = job.job;
    capture::CaptureConfig config = job.config;
    job.started = Clock::now();
    job.startInProgress = true;
    job.status = "starting";
    job.startFuture = std::async(std::launch::async, [source = std::move(source), config = std::move(config)]() mutable {
        return startJobWorker(std::move(source), std::move(config));
    });
}

bool completeStartJobIfReady(RunningJob& job) {
    if (!job.startInProgress || !job.startFuture.valid()) return false;
    if (job.startFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return false;

    const bool stopRequested = job.stopRequested;
    std::shared_ptr<RunningJob> result = job.startFuture.get();
    RunningJob completed{};
    if (result) completed = std::move(*result);
    completed.stopRequested = false;
    if (stopRequested) requestStopJob(completed);
    job = std::move(completed);
    return true;
}

void waitForStartJob(RunningJob& job) {
    if (!job.startInProgress || !job.startFuture.valid()) return;
    const bool stopRequested = job.stopRequested;
    std::shared_ptr<RunningJob> result = job.startFuture.get();
    RunningJob completed{};
    if (result) completed = std::move(*result);
    completed.stopRequested = false;
    if (stopRequested) requestStopJob(completed);
    job = std::move(completed);
}

int activeJobSlots(const std::vector<RunningJob>& jobs) noexcept {
    int count = 0;
    for (const auto& job : jobs) {
        if (job.finalized) continue;
        if (job.startInProgress || job.running) ++count;
    }
    return count;
}

bool exclusiveMarketDataSessionBlocked(const RunningJob& candidate, const std::vector<RunningJob>& jobs) {
    const std::string key = tui::exclusiveMarketDataSessionKey(candidate.job);
    if (key.empty()) return false;
    for (const auto& other : jobs) {
        if (&other == &candidate || other.finalized) continue;
        if (!other.startInProgress && !other.running) continue;
        if (tui::exclusiveMarketDataSessionKey(other.job) == key) return true;
    }
    return false;
}

bool allFinalized(const std::vector<RunningJob>& jobs) {
    for (const auto& job : jobs) {
        if (!job.finalized) return false;
    }
    return true;
}

std::uint64_t totalRows(const std::vector<RunningJob>& jobs) noexcept {
    std::uint64_t rows = 0;
    for (const auto& job : jobs) {
        if (job.coordinator) rows += totalRows(*job.coordinator);
    }
    return rows;
}

void writeStatusFile(const std::filesystem::path& path,
                     const std::vector<RunningJob>& jobs,
                     Clock::time_point now,
                     std::string_view state,
                     std::string_view message) {
    if (path.empty()) return;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tempPath = path.string() + ".tmp";

    int running = 0;
    int starting = 0;
    int stalled = 0;
    int pending = 0;
    int errors = 0;
    int finalized = 0;
    for (const auto& job : jobs) {
        if (job.running) ++running;
        if (job.startInProgress) ++starting;
        if (startStalled(job, now)) ++stalled;
        if (!job.launched && !job.finalized) ++pending;
        if (job.status == "error" || job.status == "done_warn") ++errors;
        if (job.finalized) ++finalized;
    }

    {
        std::ofstream out(tempPath, std::ios::out | std::ios::trunc);
        out << "state=" << state << '\n';
        out << "message=" << message << '\n';
        out << "jobs=" << jobs.size() << '\n';
        out << "running=" << running << '\n';
        out << "starting=" << starting << '\n';
        out << "stalled=" << stalled << '\n';
        out << "pending=" << pending << '\n';
        out << "finalized=" << finalized << '\n';
        out << "errors=" << errors << '\n';
        out << "rows=" << totalRows(jobs) << '\n';
    }
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
    }
}

capture::CaptureChannel captureChannelForRunner(tui::LaunchChannel channel) noexcept {
    switch (channel) {
        case tui::LaunchChannel::Trades: return capture::CaptureChannel::Trades;
        case tui::LaunchChannel::Liquidations: return capture::CaptureChannel::Liquidations;
        case tui::LaunchChannel::BookTicker: return capture::CaptureChannel::BookTicker;
        case tui::LaunchChannel::Orderbook: return capture::CaptureChannel::Orderbook;
        case tui::LaunchChannel::MarkPrice: return capture::CaptureChannel::MarkPrice;
        case tui::LaunchChannel::IndexPrice: return capture::CaptureChannel::IndexPrice;
        case tui::LaunchChannel::Funding: return capture::CaptureChannel::Funding;
        case tui::LaunchChannel::PriceLimit: return capture::CaptureChannel::PriceLimit;
    }
    return capture::CaptureChannel::BookTicker;
}

struct RunnerChannelAvailabilityCacheEntry {
    std::string exchange;
    std::string market;
    std::string symbol;
    std::uint8_t apiSlot{1u};
    tui::LaunchChannel channel{tui::LaunchChannel::Trades};
    bool available{false};
};

struct RunnerChannelAvailabilityCache {
    std::vector<RunnerChannelAvailabilityCacheEntry> entries{};
};

bool runnerChannelAvailableUncached(const tui::RecorderTuiJob& job, tui::LaunchChannel channel) {
    capture::CaptureConfig config{};
    config.exchange = job.exchange;
    config.market = job.market;
    config.symbols = {job.symbol};
    config.apiSlot = 1u;
    std::string detail;
    return capture::captureChannelRuntimeReady(config, captureChannelForRunner(channel), detail);
}

bool runnerChannelAvailable(const tui::RecorderTuiJob& job, tui::LaunchChannel channel, void* userData) {
    auto* cache = static_cast<RunnerChannelAvailabilityCache*>(userData);
    if (cache == nullptr) return runnerChannelAvailableUncached(job, channel);

    const std::string exchange = lowerAscii(job.exchange);
    const std::string market = lowerAscii(job.market);
    const std::string symbol = lowerAscii(job.symbol);
    constexpr std::uint8_t apiSlot = 1u;
    for (const auto& entry : cache->entries) {
        if (entry.exchange == exchange &&
            entry.market == market &&
            entry.symbol == symbol &&
            entry.apiSlot == apiSlot &&
            entry.channel == channel) return entry.available;
    }

    const bool available = runnerChannelAvailableUncached(job, channel);
    cache->entries.push_back(RunnerChannelAvailabilityCacheEntry{
        .exchange = exchange,
        .market = market,
        .symbol = symbol,
        .apiSlot = apiSlot,
        .channel = channel,
        .available = available,
    });
    return available;
}

std::vector<RunningJob> makeJobs(const tui::RecorderTuiPreset& preset, RunOutputGroups& outputGroups) {
    RunnerChannelAvailabilityCache availabilityCache{};
    const tui::RecorderTuiLaunchPlan plan = tui::buildLaunchPlan(preset, runnerChannelAvailable, &availabilityCache);
    std::vector<RunningJob> jobs;
    jobs.reserve(plan.jobs.size());
    const auto baseTime = Clock::now();
    for (const auto& planned : plan.jobs) {
        RunningJob job{};
        job.job = planned.job;
        job.config = makeCaptureConfig(planned.job, outputDirForRunJob(outputGroups, planned.job));
        job.scheduledStart = baseTime + std::chrono::milliseconds(planned.scheduledStartMs);
        if (planned.skipJob) {
            job.status = "skipped";
            job.error = planned.skipReason;
            job.finalized = true;
        }
        jobs.push_back(std::move(job));
    }
    return jobs;
}

void writeRunGroupManifests(const std::filesystem::path& recordingsRoot, const RunOutputGroups& outputGroups) {
    std::vector<std::filesystem::path> targetGroups;
    targetGroups.reserve(outputGroups.bySymbol.size());
    for (const auto& [_, groupPath] : outputGroups.bySymbol) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(groupPath, ec);
        if (ec || canonical.empty()) continue;
        if (!std::filesystem::exists(canonical, ec)) continue;
        if (!ec && std::filesystem::is_empty(canonical, ec)) {
            std::filesystem::remove(canonical, ec);
            continue;
        }
        if (std::find(targetGroups.begin(), targetGroups.end(), canonical) == targetGroups.end()) {
            targetGroups.push_back(canonical);
        }
    }
    if (targetGroups.empty()) return;

    const auto discovery = recordings::discoverRecordings(recordingsRoot);
    for (const auto& group : discovery.groups) {
        if (std::find(targetGroups.begin(), targetGroups.end(), group.path) == targetGroups.end()) continue;
        std::string error;
        (void)recordings::writeGroupManifest(group, &error);
    }
}

int runPresetFile(const tui::RecorderTuiPreset& preset, const std::filesystem::path& statusPath) {
    std::signal(SIGINT, handlePresetRunnerSignal);
    std::signal(SIGTERM, handlePresetRunnerSignal);

    RunOutputGroups outputGroups = makeRunOutputGroups(preset);
    std::vector<RunningJob> jobs = makeJobs(preset, outputGroups);
    auto nextStatus = Clock::now();
    std::string message = "started";

    while (!gPresetRunnerStop) {
        const auto now = Clock::now();
        for (auto& job : jobs) (void)completeStartJobIfReady(job);

        int launchedThisTick = 0;
        const int launchLimit = std::max(1, preset.launchWaveSize);
        int activeSlots = activeJobSlots(jobs);
        const int maxActiveJobs = std::max(1, preset.maxActiveJobs);
        for (auto& job : jobs) {
            if (launchedThisTick >= launchLimit || activeSlots >= maxActiveJobs) break;
            if (job.launched || job.finalized || job.startInProgress || now < job.scheduledStart) continue;
            if (exclusiveMarketDataSessionBlocked(job, jobs)) continue;
            ++launchedThisTick;
            ++activeSlots;
            job.launched = true;
            startJobAsync(job);
        }

        for (auto& job : jobs) {
            if (!job.launched) continue;
            if (job.startInProgress) continue;
            if (job.coordinator) job.coordinator->reapStoppedThreads();
            if (cullDeadZeroRowJob(job, now)) continue;
            const bool channelsLive = job.coordinator && anyRunningChannel(*job.coordinator);
            if (job.running && !channelsLive) {
                job.running = false;
                if (!job.stopRequested && job.status == "running") job.status = job.error.empty() ? "idle" : "error";
            }
            if (!job.running && !job.finalized && !channelsLive) {
                finalizeJob(job);
                continue;
            }
            if (!job.running || job.job.durationMin <= 0) continue;
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count();
            if (elapsed >= job.job.durationMin * 60) requestStopJob(job);
        }

        if (now >= nextStatus) {
            writeStatusFile(statusPath, jobs, now, "running", message);
            message.clear();
            nextStatus = now + kStatusFileInterval;
        }
        if (allFinalized(jobs)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    for (auto& job : jobs) requestStopJob(job);
    for (auto& job : jobs) waitForStartJob(job);
    for (auto& job : jobs) finalizeJob(job);
    writeRunGroupManifests(preset.outputDir, outputGroups);
    writeStatusFile(statusPath,
                    jobs,
                    Clock::now(),
                    gPresetRunnerStop ? "stopped" : "done",
                    gPresetRunnerStop ? "stop requested" : "done");
    return 0;
}

void printRunPresetUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder run-preset --preset path [--status path]");
}

}  // namespace

int runPresetRunner(int argc, char** argv) {
    std::filesystem::path presetPath;
    std::filesystem::path statusPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            printRunPresetUsage();
            return 0;
        }
        if (arg == "--preset") {
            if (i + 1 >= argc) {
                std::fputs("run-preset: --preset requires a path\n", stderr);
                return 2;
            }
            presetPath = tui::resolvePresetPath(argv[++i]);
            continue;
        }
        if (arg == "--status") {
            if (i + 1 >= argc) {
                std::fputs("run-preset: --status requires a path\n", stderr);
                return 2;
            }
            statusPath = argv[++i];
            continue;
        }
        std::fprintf(stderr, "run-preset: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        printRunPresetUsage();
        return 2;
    }
    if (presetPath.empty()) {
        std::fputs("run-preset: --preset is required\n", stderr);
        return 2;
    }

    tui::RecorderTuiPreset preset{};
    std::string error;
    if (!tui::loadPresetFile(presetPath, preset, error)) {
        std::fprintf(stderr, "run-preset: %s\n", error.c_str());
        return 1;
    }
    if (preset.jobs.empty()) {
        std::fputs("run-preset: preset has no jobs\n", stderr);
        return 2;
    }
    return runPresetFile(preset, statusPath);
}

}  // namespace hftrec::app
