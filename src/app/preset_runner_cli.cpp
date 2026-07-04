#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cctype>
#include <exception>
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
    bool rootIsGroup{false};
    std::map<std::string, std::filesystem::path> bySymbol;
};

RunOutputGroups makeRunOutputGroups(const tui::RecorderTuiPreset& preset, bool outputDirIsGroup) {
    return RunOutputGroups{
        .root = preset.outputDir,
        .timestampNs = wallNowNs(),
        .rootIsGroup = outputDirIsGroup,
        .bySymbol = {},
    };
}

std::filesystem::path outputDirForRunJob(RunOutputGroups& groups, const tui::RecorderTuiJob& job) {
    std::string normalizedSymbol = recordings::recordingFolderSymbol(job.exchange, job.market, job.symbol);
    if (normalizedSymbol.empty()) normalizedSymbol = "UNKNOWN";
    const auto [it, inserted] = groups.bySymbol.emplace(normalizedSymbol, std::filesystem::path{});
    if (inserted) {
        if (groups.rootIsGroup) {
            it->second = groups.root;
        } else {
            const std::string groupName = recordings::recordingGroupFolderName(groups.timestampNs, normalizedSymbol);
            it->second = uniquePath(groups.root, groupName);
        }
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

capture::CaptureChannel captureChannelForRunner(tui::LaunchChannel channel) noexcept;

tui::LaunchChannel launchChannelForCapture(capture::CaptureChannel channel) noexcept {
    switch (channel) {
        case capture::CaptureChannel::Trades: return tui::LaunchChannel::Trades;
        case capture::CaptureChannel::Liquidations: return tui::LaunchChannel::Liquidations;
        case capture::CaptureChannel::BookTicker: return tui::LaunchChannel::BookTicker;
        case capture::CaptureChannel::Orderbook: return tui::LaunchChannel::Orderbook;
        case capture::CaptureChannel::MarkPrice: return tui::LaunchChannel::MarkPrice;
        case capture::CaptureChannel::IndexPrice: return tui::LaunchChannel::IndexPrice;
        case capture::CaptureChannel::Funding: return tui::LaunchChannel::Funding;
        case capture::CaptureChannel::PriceLimit: return tui::LaunchChannel::PriceLimit;
    }
    return tui::LaunchChannel::BookTicker;
}

std::vector<capture::CaptureChannel> selectedCaptureChannels(const tui::ChannelSelection& channels) {
    std::vector<capture::CaptureChannel> selected;
    selected.reserve(8u);
    for (const auto channel : {
             tui::LaunchChannel::Trades,
             tui::LaunchChannel::Liquidations,
             tui::LaunchChannel::BookTicker,
             tui::LaunchChannel::Orderbook,
             tui::LaunchChannel::MarkPrice,
             tui::LaunchChannel::IndexPrice,
             tui::LaunchChannel::Funding,
             tui::LaunchChannel::PriceLimit,
         }) {
        if (tui::launchChannelSelected(channels, channel)) selected.push_back(captureChannelForRunner(channel));
    }
    return selected;
}

void appendJobError(RunningJob& job, std::string message) {
    if (message.empty()) return;
    if (!job.error.empty()) job.error += " | ";
    job.error += std::move(message);
}

void applyPreflightPlan(RunningJob& job, const capture::CaptureLaunchPlan& plan) {
    for (const auto& decision : plan.decisions) {
        if (!decision.skipped) continue;
        tui::setLaunchChannel(job.job.channels, launchChannelForCapture(decision.channel), false);
    }
    if (const std::string summary = plan.skippedSummary(); !summary.empty()) {
        appendJobError(job, "preflight: " + summary);
    }
}

bool preflightJobBeforeSession(RunningJob& job) {
    const auto requested = selectedCaptureChannels(job.job.channels);
    if (requested.empty()) {
        job.status = "preflight_failed";
        job.error = "preflight: no selected market-data channels";
        job.running = false;
        job.finalized = true;
        return false;
    }

    const capture::CaptureLaunchPlan plan = capture::preflightCaptureLaunchPlan(job.config, requested);
    applyPreflightPlan(job, plan);
    if (plan.anyEnabled()) return true;

    job.status = "preflight_failed";
    if (job.error.empty()) job.error = "preflight: no market-data channels passed startup";
    job.running = false;
    job.finalized = true;
    return false;
}

RunningJob startJobFromConfig(const tui::RecorderTuiJob& source, capture::CaptureConfig config) {
    RunningJob job{};
    job.job = source;
    job.config = std::move(config);
    job.started = Clock::now();
    job.scheduledStart = job.started;
    job.launched = true;
    job.status = "preflighting";

    if (!preflightJobBeforeSession(job)) return job;

    job.coordinator = std::make_unique<capture::CaptureCoordinator>();
    job.status = "starting";

    if (job.job.channels.trades) tryStartChannel(job, "trades", job.coordinator->startTrades(job.config));
    if (job.job.channels.liquidations) tryStartChannel(job, "liquidations", job.coordinator->startLiquidations(job.config));
    if (job.job.channels.bookTicker) tryStartChannel(job, "bookticker", job.coordinator->startBookTicker(job.config));
    if (job.job.channels.orderbook) tryStartChannel(job, "orderbook", job.coordinator->startOrderbook(job.config));
    if (job.job.channels.markPrice) tryStartChannel(job, "mark_price", job.coordinator->startMarkPrice(job.config));
    if (job.job.channels.indexPrice) tryStartChannel(job, "index_price", job.coordinator->startIndexPrice(job.config));
    if (job.job.channels.funding) tryStartChannel(job, "funding", job.coordinator->startFunding(job.config));
    if (job.job.channels.priceLimit) tryStartChannel(job, "price_limit", job.coordinator->startPriceLimit(job.config));

    job.running = anyRunningChannel(*job.coordinator);
    if (!job.running && job.error.empty()) job.error = "no channels started";
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
    } else if (totalRows(*job.coordinator) == 0u) {
        const auto error = job.coordinator->lastError();
        if (!error.empty()) {
            job.error = error;
        } else if (job.error.empty()) {
            job.error = "no canonical rows captured";
        }
        job.status = "failed_empty";
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

void markStartJobException(RunningJob& job, std::string error) {
    job.startInProgress = false;
    job.running = false;
    job.finalized = true;
    job.status = "error";
    if (job.error.empty()) {
        job.error = "startup exception: " + error;
    } else {
        job.error += " | startup exception: " + error;
    }
}

void applyCompletedStartJob(RunningJob& job, std::shared_ptr<RunningJob> result, bool stopRequested) {
    RunningJob completed{};
    if (result) completed = std::move(*result);
    completed.stopRequested = false;
    if (stopRequested) requestStopJob(completed);
    job = std::move(completed);
}

void startJobAsync(RunningJob& job) {
    tui::RecorderTuiJob source = job.job;
    capture::CaptureConfig config = job.config;
    job.started = Clock::now();
    job.startInProgress = true;
    job.status = "starting";
    try {
        job.startFuture = std::async(std::launch::async, [source = std::move(source), config = std::move(config)]() mutable {
            return startJobWorker(std::move(source), std::move(config));
        });
    } catch (const std::exception& ex) {
        markStartJobException(job, ex.what());
    } catch (...) {
        markStartJobException(job, "unknown exception");
    }
}

bool completeStartJobIfReady(RunningJob& job) {
    if (!job.startInProgress || !job.startFuture.valid()) return false;
    if (job.startFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return false;

    const bool stopRequested = job.stopRequested;
    try {
        applyCompletedStartJob(job, job.startFuture.get(), stopRequested);
    } catch (const std::exception& ex) {
        markStartJobException(job, ex.what());
    } catch (...) {
        markStartJobException(job, "unknown exception");
    }
    return true;
}

void waitForStartJob(RunningJob& job) {
    if (!job.startInProgress || !job.startFuture.valid()) return;
    const bool stopRequested = job.stopRequested;
    try {
        applyCompletedStartJob(job, job.startFuture.get(), stopRequested);
    } catch (const std::exception& ex) {
        markStartJobException(job, ex.what());
    } catch (...) {
        markStartJobException(job, "unknown exception");
    }
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

int startJobsInProgress(const std::vector<RunningJob>& jobs) noexcept {
    int count = 0;
    for (const auto& job : jobs) {
        if (job.startInProgress) ++count;
    }
    return count;
}

void markStartJobsStopped(std::vector<RunningJob>& jobs) {
    for (auto& job : jobs) {
        if (!job.startInProgress) continue;
        job.stopRequested = true;
        job.startInProgress = false;
        job.running = false;
        job.finalized = true;
        job.status = "stopped_starting";
        if (job.error.empty()) job.error = "stop requested while startup was still in progress";
    }
}

void finalizeNonStartingJobs(std::vector<RunningJob>& jobs) {
    for (auto& job : jobs) {
        if (job.startInProgress) continue;
        finalizeJob(job);
    }
}

std::uint64_t totalRows(const std::vector<RunningJob>& jobs) noexcept {
    std::uint64_t rows = 0;
    for (const auto& job : jobs) {
        if (job.coordinator) rows += totalRows(*job.coordinator);
    }
    return rows;
}

struct PresetStatusSummary {
    int running{0};
    int starting{0};
    int stalled{0};
    int pending{0};
    int errors{0};
    int finalized{0};
    int skipped{0};
    std::string firstError{};
    std::string lastError{};
};

bool jobStatusIsError(std::string_view status) noexcept {
    return status == "error"
        || status == "done_warn"
        || status == "dead"
        || status == "failed_empty"
        || status == "preflight_failed"
        || status == "stopped_starting";
}

PresetStatusSummary summarizePresetStatus(const std::vector<RunningJob>& jobs, Clock::time_point now) {
    PresetStatusSummary summary{};
    for (const auto& job : jobs) {
        if (job.running) ++summary.running;
        if (job.startInProgress) ++summary.starting;
        if (startStalled(job, now)) ++summary.stalled;
        if (!job.launched && !job.finalized) ++summary.pending;
        if (jobStatusIsError(job.status)) ++summary.errors;
        if (job.finalized) ++summary.finalized;
        if (job.status == "skipped") ++summary.skipped;
        if (!job.error.empty()) {
            if (summary.firstError.empty()) summary.firstError = job.error;
            summary.lastError = job.error;
        }
    }
    return summary;
}

bool allJobsSkipped(const std::vector<RunningJob>& jobs, const PresetStatusSummary& summary) noexcept {
    return !jobs.empty()
        && summary.finalized == static_cast<int>(jobs.size())
        && summary.skipped == static_cast<int>(jobs.size());
}

std::string finalErrorMessage(const PresetStatusSummary& summary) {
    if (summary.firstError.empty()) return "completed with errors";
    if (summary.errors <= 1) return summary.firstError;
    return "completed with errors: " + summary.firstError;
}

std::string statusLineValue(std::string_view value) {
    std::string out{value};
    for (char& ch : out) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    return out;
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

    const PresetStatusSummary summary = summarizePresetStatus(jobs, now);

    {
        std::ofstream out(tempPath, std::ios::out | std::ios::trunc);
        out << "state=" << state << '\n';
        out << "message=" << statusLineValue(message) << '\n';
        out << "jobs=" << jobs.size() << '\n';
        out << "running=" << summary.running << '\n';
        out << "starting=" << summary.starting << '\n';
        out << "stalled=" << summary.stalled << '\n';
        out << "pending=" << summary.pending << '\n';
        out << "finalized=" << summary.finalized << '\n';
        out << "errors=" << summary.errors << '\n';
        out << "skipped=" << summary.skipped << '\n';
        out << "rows=" << totalRows(jobs) << '\n';
        out << "first_error=" << statusLineValue(summary.firstError) << '\n';
        out << "last_error=" << statusLineValue(summary.lastError) << '\n';
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
    const std::string symbol = lowerAscii(tui::routeSymbolForJob(job));
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
    for (const auto& [_, groupPath] : outputGroups.bySymbol) {
        std::string error;
        (void)recordings::writeGroupManifestForPath(recordingsRoot, groupPath, &error);
    }
}

int runPresetFile(const tui::RecorderTuiPreset& preset,
                  const std::filesystem::path& statusPath,
                  bool outputDirIsGroup) {
    std::signal(SIGINT, handlePresetRunnerSignal);
    std::signal(SIGTERM, handlePresetRunnerSignal);

    RunOutputGroups outputGroups = makeRunOutputGroups(preset, outputDirIsGroup);
    std::vector<RunningJob> jobs = makeJobs(preset, outputGroups);
    writeStatusFile(statusPath, jobs, Clock::now(), "starting", "plan built");
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
    for (auto& job : jobs) (void)completeStartJobIfReady(job);

    const int blockedStarts = startJobsInProgress(jobs);
    if (blockedStarts == 0) {
        for (auto& job : jobs) waitForStartJob(job);
    } else {
        markStartJobsStopped(jobs);
    }
    finalizeNonStartingJobs(jobs);
    const PresetStatusSummary finalSummary = summarizePresetStatus(jobs, Clock::now());
    const bool skippedOnly = allJobsSkipped(jobs, finalSummary);
    const std::string finalState = gPresetRunnerStop
        ? std::string{"stopped"}
        : (skippedOnly ? std::string{"skipped"}
                       : (finalSummary.errors != 0 ? std::string{"error"} : std::string{"done"}));
    const std::string finalMessage = blockedStarts > 0
        ? std::string{"stop requested while startup was still in progress"}
        : (gPresetRunnerStop
               ? std::string{"stop requested"}
               : (skippedOnly ? std::string{"all jobs skipped: no supported channels"}
                              : (finalSummary.errors != 0 ? finalErrorMessage(finalSummary) : std::string{"done"})));
    writeStatusFile(statusPath,
                    jobs,
                    Clock::now(),
                    finalState,
                    finalMessage);
    if (blockedStarts == 0 && !gPresetRunnerStop) {
        const std::filesystem::path discoveryRoot =
            outputDirIsGroup && !preset.outputDir.parent_path().empty() ? preset.outputDir.parent_path() : preset.outputDir;
        writeRunGroupManifests(discoveryRoot, outputGroups);
    }
    if (blockedStarts > 0) {
        // std::future from std::async blocks in its destructor; leave after status is on disk.
        std::_Exit(0);
    }
    return 0;
}

void printRunPresetUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder run-preset --preset path [--status path] [--output-is-group]");
}

}  // namespace

int runPresetRunner(int argc, char** argv) {
    std::filesystem::path presetPath;
    std::filesystem::path statusPath;
    bool outputDirIsGroup = false;
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
        if (arg == "--output-is-group") {
            outputDirIsGroup = true;
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
    return runPresetFile(preset, statusPath, outputDirIsGroup);
}

}  // namespace hftrec::app
