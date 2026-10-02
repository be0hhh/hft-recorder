#include "RecorderTuiInternal.hpp"

namespace hftrec::app::tui_detail {

capture::CaptureChannel captureChannelForLaunch(tui::LaunchChannel channel) noexcept {
    switch (channel) {
        case tui::LaunchChannel::Trades: return capture::CaptureChannel::Trades;
        case tui::LaunchChannel::BookTicker: return capture::CaptureChannel::BookTicker;
        case tui::LaunchChannel::Orderbook: return capture::CaptureChannel::Orderbook;
    }
    return capture::CaptureChannel::BookTicker;
}

tui::LaunchChannel launchChannelForCapture(capture::CaptureChannel channel) noexcept {
    switch (channel) {
        case capture::CaptureChannel::Trades: return tui::LaunchChannel::Trades;
        case capture::CaptureChannel::BookTicker: return tui::LaunchChannel::BookTicker;
        case capture::CaptureChannel::Orderbook: return tui::LaunchChannel::Orderbook;
    }
    return tui::LaunchChannel::BookTicker;
}

std::vector<capture::CaptureChannel> selectedCaptureChannels(const tui::ChannelSelection& channels) {
    std::vector<capture::CaptureChannel> selected;
    selected.reserve(8u);
    for (const auto channel : {
             tui::LaunchChannel::Trades,
             tui::LaunchChannel::BookTicker,
             tui::LaunchChannel::Orderbook,
         }) {
        if (tui::launchChannelSelected(channels, channel)) selected.push_back(captureChannelForLaunch(channel));
    }
    return selected;
}

bool tuiChannelAvailableUncached(const tui::RecorderTuiJob& job, tui::LaunchChannel channel) {
    capture::CaptureConfig config{};
    config.exchange = job.exchange;
    config.market = job.market;
    config.symbols = {job.symbol};
    config.apiSlot = 1u;
    std::string detail;
    return capture::captureChannelRuntimeReady(config, captureChannelForLaunch(channel), detail);
}

bool tuiChannelAvailable(const tui::RecorderTuiJob& job, tui::LaunchChannel channel, void* userData) {
    auto* cache = static_cast<TuiChannelAvailabilityCache*>(userData);
    if (cache == nullptr) return tuiChannelAvailableUncached(job, channel);

    const std::string exchange = lower(job.exchange);
    const std::string market = lower(job.market);
    const std::string symbol = lower(tui::routeSymbolForJob(job));
    constexpr std::uint8_t apiSlot = 1u;
    for (const auto& entry : cache->entries) {
        if (entry.exchange == exchange &&
            entry.market == market &&
            entry.symbol == symbol &&
            entry.apiSlot == apiSlot &&
            entry.channel == channel) return entry.available;
    }

    const bool available = tuiChannelAvailableUncached(job, channel);
    cache->entries.push_back(TuiChannelAvailabilityCacheEntry{
        .exchange = exchange,
        .market = market,
        .symbol = symbol,
        .apiSlot = apiSlot,
        .channel = channel,
        .available = available,
    });
    return available;
}

RunOutputGroups makeRunOutputGroups(const tui::RecorderTuiPreset& preset) {
    return RunOutputGroups{.root = preset.outputDir, .timestampNs = wallNowNs()};
}

std::filesystem::path outputDirForRunJob(RunOutputGroups& groups, const tui::RecorderTuiJob& job) {
    std::string normalizedSymbol = recordings::recordingFolderSymbol(job.exchange, job.market, job.symbol);
    if (normalizedSymbol.empty()) normalizedSymbol = "UNKNOWN";

    auto [it, inserted] = groups.bySymbol.try_emplace(normalizedSymbol);
    if (inserted) {
        const std::string groupName = recordings::recordingGroupFolderName(groups.timestampNs, normalizedSymbol);
        it->second = uniquePath(groups.root, groupName);
    }
    return it->second;
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

std::int64_t startAgeSeconds(const RunningJob& job, Clock::time_point now) noexcept {
    if (!job.startInProgress) return 0;
    return std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count());
}

bool startStalled(const RunningJob& job, Clock::time_point now) noexcept {
    return job.startInProgress && startAgeSeconds(job, now) >= kStartSlotGraceSec;
}

std::string skippedChannelsNote(const tui::ChannelSelection& channels) {
    if (!tui::anyChannelSelected(channels)) return {};
    return "skipped channels: " + tui::renderChannelSelection(channels);
}

void mergeChannelSelection(tui::ChannelSelection& target, const tui::ChannelSelection& source) noexcept {
    target.trades = target.trades || source.trades;
    target.bookTicker = target.bookTicker || source.bookTicker;
    target.orderbook = target.orderbook || source.orderbook;
}

void appendPlanNote(std::string& target, const std::string& note) {
    if (note.empty()) return;
    if (!target.empty()) target += " | ";
    target += note;
}

std::string launchPlanMessage(const tui::RecorderTuiLaunchPlan& plan) {
    std::ostringstream out;
    out << "planned " << plan.runnableJobs << " job(s)";
    if (plan.skippedJobs != 0u) out << ", skipped " << plan.skippedJobs;
    out << ", wave=" << plan.launchWaveSize << ", stagger=" << plan.launchStaggerMs
        << "ms, same-exchange=" << plan.sameExchangeCooldownMs << "ms"
        << ", max-active=" << plan.maxActiveJobs;
    return out.str();
}

RunningJob makePlannedJob(const tui::RecorderTuiLaunchJob& planned,
                          Clock::time_point baseTime,
                          RunOutputGroups& outputGroups) {
    RunningJob job{};
    job.job = planned.job;
    job.config = makeCaptureConfig(planned.job, outputDirForRunJob(outputGroups, planned.job));
    job.scheduledStart = baseTime + std::chrono::milliseconds(planned.scheduledStartMs);
    job.skippedChannels = planned.skippedChannels;
    job.planNote = skippedChannelsNote(planned.skippedChannels);
    if (planned.skipJob) {
        job.status = "skipped";
        job.planNote = planned.skipReason;
        job.finalized = true;
    } else {
        job.status = "pending";
    }
    return job;
}

void appendPlannedJobs(std::vector<RunningJob>& jobs,
                       const tui::RecorderTuiLaunchPlan& plan,
                       Clock::time_point baseTime,
                       RunOutputGroups& outputGroups) {
    jobs.reserve(jobs.size() + plan.jobs.size());
    for (const auto& planned : plan.jobs) jobs.push_back(makePlannedJob(planned, baseTime, outputGroups));
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

void appendJobError(RunningJob& job, std::string message) {
    if (message.empty()) return;
    if (!job.error.empty()) job.error += " | ";
    job.error += std::move(message);
}

void applyPreflightPlan(RunningJob& job, const capture::CaptureLaunchPlan& plan) {
    for (const auto& decision : plan.decisions) {
        if (!decision.skipped) continue;
        tui::setLaunchChannel(job.job.channels, launchChannelForCapture(decision.channel), false);
        tui::setLaunchChannel(job.skippedChannels, launchChannelForCapture(decision.channel), true);
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

bool anyRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesRunning() || coordinator.bookTickerRunning() ||
           coordinator.orderbookRunning();
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
        if (job.status == "pending") job.status = "stopped";
        return;
    }
    if (job.job.channels.trades) (void)job.coordinator->requestStopTrades();
    if (job.job.channels.bookTicker) (void)job.coordinator->requestStopBookTicker();
    if (job.job.channels.orderbook) (void)job.coordinator->requestStopOrderbook();
    job.stopRequested = true;
    job.running = false;
    job.status = "stopping";
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
    if (job.job.channels.bookTicker) tryStartChannel(job, "bookticker", job.coordinator->startBookTicker(job.config));
    if (job.job.channels.orderbook) tryStartChannel(job, "orderbook", job.coordinator->startOrderbook(job.config));

    job.running = anyRunningChannel(*job.coordinator);
    job.status = job.running ? "running" : "error";
    return job;
}

bool preparePlannedJobChannels(RunningJob& job, TuiChannelAvailabilityCache& availabilityCache) {
    tui::RecorderTuiLaunchJob planned{};
    planned.job = job.job;
    const tui::RecorderTuiLaunchJob filtered =
        tui::filterLaunchJobChannels(planned, tuiChannelAvailable, &availabilityCache);
    job.job = filtered.job;
    job.skippedChannels = filtered.skippedChannels;
    job.planNote = skippedChannelsNote(filtered.skippedChannels);
    if (!filtered.skipJob) return true;

    job.status = "skipped";
    job.planNote = filtered.skipReason;
    job.running = false;
    job.finalized = true;
    return false;
}

std::shared_ptr<RunningJob> prepareAndStartJobWorker(tui::RecorderTuiJob source,
                                                     capture::CaptureConfig config,
                                                     Clock::time_point scheduledStart) {
    RunningJob job{};
    job.job = std::move(source);
    job.config = std::move(config);
    job.scheduledStart = scheduledStart;

    TuiChannelAvailabilityCache availabilityCache{};
    if (!preparePlannedJobChannels(job, availabilityCache)) {
        return std::make_shared<RunningJob>(std::move(job));
    }

    RunningJob started = startJobFromConfig(job.job, std::move(job.config));
    started.scheduledStart = scheduledStart;
    mergeChannelSelection(started.skippedChannels, job.skippedChannels);
    std::string planNote = job.planNote;
    appendPlanNote(planNote, started.planNote);
    started.planNote = std::move(planNote);
    return std::make_shared<RunningJob>(std::move(started));
}

void startPlannedJobAsync(RunningJob& job) {
    tui::RecorderTuiJob source = job.job;
    capture::CaptureConfig config = job.config;
    const Clock::time_point scheduledStart = job.scheduledStart;
    job.started = Clock::now();
    job.startInProgress = true;
    job.status = "starting";
    job.startFuture = std::async(std::launch::async,
                                 [source = std::move(source),
                                  config = std::move(config),
                                  scheduledStart]() mutable {
                                     return prepareAndStartJobWorker(std::move(source),
                                                                     std::move(config),
                                                                     scheduledStart);
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

void writeRunGroupManifests(const std::filesystem::path& recordingsRoot, const RunOutputGroups& outputGroups) {
    for (const auto& [_, groupPath] : outputGroups.bySymbol) {
        std::string error;
        (void)recordings::writeGroupManifestForPath(recordingsRoot, groupPath, &error);
    }
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
    job.status = "stopping";
    job.finalRows = totalRows(*job.coordinator);
    const auto status = job.coordinator->finalizeSession();
    if (!isOk(status)) {
        const auto error = job.coordinator->lastError();
        job.error = error.empty() ? std::string(statusToString(status)) : error;
        job.status = "error";
    } else if (job.finalRows == 0u) {
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

std::uint64_t totalRows(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesCount() + coordinator.bookTickerCount() +
           coordinator.depthCount();
}

bool allJobsFinalized(const std::vector<RunningJob>& jobs) noexcept {
    return std::all_of(jobs.begin(), jobs.end(), [](const RunningJob& job) { return job.finalized; });
}

void runJobs(TerminalGuard& terminal, const tui::RecorderTuiPreset& preset) {
    tui::RecorderTuiPreset runPreset = preset;
    RunOutputGroups outputGroups = makeRunOutputGroups(runPreset);
    const tui::RecorderTuiLaunchPlan launchPlan = tui::buildLaunchPlan(runPreset, nullptr, nullptr);
    std::vector<RunningJob> jobs;
    jobs.reserve(launchPlan.jobs.size());
    appendPlannedJobs(jobs, launchPlan, Clock::now(), outputGroups);

    std::size_t selected = 0;
    std::string message = launchPlanMessage(launchPlan);
    if (const std::string cleanup = deadSessionSweepMessage(sweepDeadZeroRowSessions(runPreset.outputDir, wallNowNs()));
        !cleanup.empty()) {
        message += " | " + cleanup;
    }
    renderRunning(jobs, selected, message);
    message.clear();
    auto nextProgress = Clock::now() + std::chrono::seconds(std::max(1, preset.progressSec));
    auto nextDeadSessionSweep = Clock::now() + kDeadZeroRowSweepInterval;
    bool dirty = false;
    bool stopRequestedByUser = false;
    std::string lastIdleMessage;
    while (true) {
        if (gInterrupted) {
            stopRequestedByUser = true;
            for (auto& job : jobs) requestStopJob(job);
            renderRunning(jobs, selected, "interrupt received; stop requested for all jobs");
            break;
        }

        const auto now = Clock::now();
        bool stateChanged = false;
        for (auto& job : jobs) {
            if (completeStartJobIfReady(job)) stateChanged = true;
        }
        int launchedThisTick = 0;
        const int launchLimit = std::max(1, runPreset.launchWaveSize);
        int activeSlots = activeJobSlots(jobs);
        const int maxActiveJobs = std::max(1, runPreset.maxActiveJobs);
        for (auto& job : jobs) {
            if (launchedThisTick >= launchLimit) break;
            if (activeSlots >= maxActiveJobs) break;
            if (job.launched || job.finalized || job.startInProgress || now < job.scheduledStart) continue;
            if (exclusiveMarketDataSessionBlocked(job, jobs)) continue;
            ++launchedThisTick;
            ++activeSlots;
            startPlannedJobAsync(job);
            stateChanged = true;
        }
        for (auto& job : jobs) {
            if (!job.launched) continue;
            if (job.startInProgress) continue;
            if (job.coordinator) job.coordinator->reapStoppedThreads();
            if (cullDeadZeroRowJob(job, now)) {
                message = "removed dead zero-row session";
                stateChanged = true;
                continue;
            }
            const bool channelsLive = job.coordinator && anyRunningChannel(*job.coordinator);
            if (job.running && !channelsLive) {
                job.running = false;
                if (!job.stopRequested && job.status == "running") job.status = job.error.empty() ? "idle" : "error";
                stateChanged = true;
            }
            if (!job.running && !job.finalized && !channelsLive) {
                finalizeJob(job);
                stateChanged = true;
                continue;
            }
            if (!job.running || job.job.durationMin <= 0) continue;
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count();
            if (elapsed >= job.job.durationMin * 60) {
                requestStopJob(job);
                stateChanged = true;
            }
        }
        if (now >= nextDeadSessionSweep) {
            const DeadSessionSweepResult cleanup = sweepRunOutputGroups(outputGroups);
            if (const std::string cleanupMessage = deadSessionSweepMessage(cleanup); !cleanupMessage.empty()) {
                message = cleanupMessage;
                stateChanged = true;
            }
            nextDeadSessionSweep = now + kDeadZeroRowSweepInterval;
        }

        if (dirty || stateChanged || now >= nextProgress) {
            renderRunning(jobs, selected, message);
            message.clear();
            dirty = false;
            nextProgress = now + std::chrono::seconds(std::max(1, preset.progressSec));
        }

        const Key key = readKey(200);
        if (key.kind == KeyKind::Up && selected > 0) {
            --selected;
            dirty = true;
        } else if (key.kind == KeyKind::Down && selected + 1u < jobs.size()) {
            ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == '+') {
            const std::string input = promptLine(terminal, "symbols or .ini list");
            if (input.empty()) {
                message = "symbol generation canceled";
            } else {
                const std::size_t before = runPreset.jobs.size();
                const GeneratedJobsAppendResult result = appendGeneratedSymbolJobs(runPreset, input);
                if (before < runPreset.jobs.size()) {
                    tui::RecorderTuiPreset addPreset = runPreset;
                    addPreset.jobs.assign(runPreset.jobs.begin() + static_cast<std::ptrdiff_t>(before),
                                          runPreset.jobs.end());
                    const tui::RecorderTuiLaunchPlan addPlan =
                        tui::buildLaunchPlan(addPreset, nullptr, nullptr);
                    appendPlannedJobs(jobs, addPlan, Clock::now(), outputGroups);
                }
                if (selected >= jobs.size()) selected = jobs.empty() ? 0 : jobs.size() - 1u;
                message = generatedJobsMessage(result, "scheduled");
            }
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 's' || key.ch == 'S' || key.ch == 'c' || key.ch == 'C')) {
            if (selected < jobs.size()) requestStopJob(jobs[selected]);
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'a' || key.ch == 'A')) {
            for (auto& job : jobs) requestStopJob(job);
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'q' || key.ch == 'Q')) {
            stopRequestedByUser = !allJobsFinalized(jobs);
            for (auto& job : jobs) requestStopJob(job);
            renderRunning(jobs, selected, "stop requested; finalizing sessions");
            break;
        }

        const bool anyLive = std::any_of(jobs.begin(), jobs.end(), [](const RunningJob& job) { return job.running; });
        const bool anyStarting =
            std::any_of(jobs.begin(), jobs.end(), [](const RunningJob& job) { return job.startInProgress; });
        const auto stalledStartsForMessage =
            std::count_if(jobs.begin(), jobs.end(), [now](const RunningJob& job) { return startStalled(job, now); });
        const bool anyStalled = stalledStartsForMessage > 0;
        const bool anyScheduled = std::any_of(jobs.begin(), jobs.end(), [](const RunningJob& job) {
            return !job.launched && !job.finalized && !job.startInProgress;
        });
        const bool anyPending = std::any_of(jobs.begin(), jobs.end(), [](const RunningJob& job) { return !job.finalized; });
        std::string idleMessage;
        if (anyStalled && anyScheduled) {
            idleMessage = "some starts stalled; scheduled jobs continue";
        } else if (!anyLive && anyStarting) {
            idleMessage = "starting jobs";
        } else if (!anyLive && anyScheduled) {
            idleMessage = "waiting for scheduled starts";
        } else if (!anyLive && anyPending) {
            idleMessage = "stop requested; finalizing sessions";
        } else if (!anyLive) {
            idleMessage = "all jobs finalized; press q to return";
        }
        if (idleMessage.empty()) {
            lastIdleMessage.clear();
        } else if (idleMessage != lastIdleMessage) {
            message = idleMessage;
            lastIdleMessage = idleMessage;
            dirty = true;
        }
    }
    for (auto& job : jobs) requestStopJob(job);
    for (auto& job : jobs) waitForStartJob(job);
    for (auto& job : jobs) finalizeJob(job);

    if (!stopRequestedByUser) writeRunGroupManifests(runPreset.outputDir, outputGroups);
}

}  // namespace hftrec::app::tui_detail
