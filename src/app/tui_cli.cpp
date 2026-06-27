#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/capture/CaptureChannelSupport.hpp"
#include "core/capture/CaptureCoordinator.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/tui/RecorderTuiLaunch.hpp"
#include "core/tui/RecorderTuiPreset.hpp"
#include "core/tui/RecorderTuiSymbols.hpp"
#include "core/tui/TerminalRender.hpp"

namespace hftrec::app {

int runShardPresetInteractive(const tui::RecorderTuiPreset& preset, const std::filesystem::path& presetPath);

namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t gInterrupted = 0;

void handleSignal(int) {
    gInterrupted = 1;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1u]))) --end;
    return std::string{text.substr(begin, end - begin)};
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
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
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return parent / (baseName + "_overflow");
}

class TerminalGuard {
  public:
    TerminalGuard() {
        interactive_ = ::isatty(STDIN_FILENO) == 1 && ::isatty(STDOUT_FILENO) == 1;
        if (!interactive_) return;
        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            interactive_ = false;
            return;
        }
        raw_ = original_;
        raw_.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw_.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw_.c_cc[VMIN] = 0;
        raw_.c_cc[VTIME] = 0;
        resume();
        std::fputs("\033[?1049h\033[2J\033[H\033[?25l", stdout);
        alternateScreen_ = true;
        std::fflush(stdout);
    }

    ~TerminalGuard() {
        if (!interactive_) return;
        suspend();
        std::fputs("\033[?25h\033[0m", stdout);
        if (alternateScreen_) std::fputs("\033[?1049l", stdout);
        std::putchar('\n');
        std::fflush(stdout);
    }

    bool interactive() const noexcept { return interactive_; }

    void suspend() noexcept {
        if (interactive_) (void)::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }

    void resume() noexcept {
        if (interactive_) (void)::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_);
    }

  private:
    bool interactive_{false};
    bool alternateScreen_{false};
    termios original_{};
    termios raw_{};
};

enum class KeyKind {
    None,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Character,
};

struct Key {
    KeyKind kind{KeyKind::None};
    char ch{0};
};

Key readKey(int timeoutMs) {
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, timeoutMs);
    if (ready <= 0 || (pfd.revents & POLLIN) == 0) return {};

    char ch = 0;
    if (::read(STDIN_FILENO, &ch, 1) != 1) return {};
    if (ch == '\r' || ch == '\n') return {KeyKind::Enter, 0};
    if (ch == 0x7f || ch == '\b') return {KeyKind::Backspace, 0};
    if (ch == 0x1b) {
        char seq[2]{};
        if (::read(STDIN_FILENO, &seq[0], 1) != 1) return {KeyKind::Escape, 0};
        if (::read(STDIN_FILENO, &seq[1], 1) != 1) return {KeyKind::Escape, 0};
        if (seq[0] == '[') {
            if (seq[1] == 'A') return {KeyKind::Up, 0};
            if (seq[1] == 'B') return {KeyKind::Down, 0};
            if (seq[1] == 'C') return {KeyKind::Right, 0};
            if (seq[1] == 'D') return {KeyKind::Left, 0};
        }
        return {KeyKind::Escape, 0};
    }
    if (ch == 3) {
        gInterrupted = 1;
        return {};
    }
    return {KeyKind::Character, ch};
}

void clearScreen() {
    std::fputs("\033[2J\033[H", stdout);
}

tui::TerminalViewport currentViewport() noexcept {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0 && size.ws_col > 0) {
        return tui::sanitizeViewport(
            tui::TerminalViewport{.rows = static_cast<int>(size.ws_row), .cols = static_cast<int>(size.ws_col)});
    }
    return tui::sanitizeViewport({});
}

void printLine(std::string_view line, tui::TerminalViewport viewport) {
    const std::string text = tui::truncateForTerminal(line, viewport.cols);
    std::fputs(text.c_str(), stdout);
    std::putchar('\n');
}

std::string promptLine(TerminalGuard& terminal, std::string_view label, std::string_view current = {}) {
    terminal.suspend();
    std::fputs("\033[?25h", stdout);
    std::printf("\n%s", std::string(label).c_str());
    if (!current.empty()) std::printf(" [%s]", std::string(current).c_str());
    std::printf(": ");
    std::fflush(stdout);

    std::string line;
    std::getline(std::cin, line);
    std::fputs("\033[?25l", stdout);
    terminal.resume();
    line = trim(line);
    return line.empty() ? std::string{current} : line;
}

std::string jobLabel(const tui::RecorderTuiJob& job) {
    return job.name + " | " + job.exchange + "/" + job.market + " " + job.symbol +
           " | " + (job.durationMin == 0 ? "until stop" : std::to_string(job.durationMin) + "m") +
           " | " + tui::renderChannelSelection(job.channels);
}

bool sameCaptureJob(const tui::RecorderTuiJob& lhs, const tui::RecorderTuiJob& rhs) {
    return lower(lhs.exchange) == lower(rhs.exchange)
        && lower(lhs.market) == lower(rhs.market)
        && lower(lhs.symbol) == lower(rhs.symbol);
}

bool containsCaptureJob(const std::vector<tui::RecorderTuiJob>& jobs, const tui::RecorderTuiJob& candidate) {
    return std::any_of(jobs.begin(), jobs.end(), [&](const tui::RecorderTuiJob& existing) {
        return sameCaptureJob(existing, candidate);
    });
}

void addDefaultJob(tui::RecorderTuiPreset& preset) {
    tui::RecorderTuiJob job{};
    job.name = "job" + std::to_string(preset.jobs.size() + 1u);
    job.channels = tui::allLiveChannels();
    preset.jobs.push_back(std::move(job));
}

void duplicateJob(tui::RecorderTuiPreset& preset, std::size_t index) {
    if (index >= preset.jobs.size()) return;
    auto copy = preset.jobs[index];
    copy.name += "_copy";
    preset.jobs.insert(preset.jobs.begin() + static_cast<std::ptrdiff_t>(index + 1u), std::move(copy));
}

struct GeneratedJobsAppendResult {
    std::size_t symbols{0};
    std::size_t added{0};
    std::size_t skipped{0};
    std::vector<std::filesystem::path> loadedFiles{};
    std::string error{};
};

GeneratedJobsAppendResult appendGeneratedSymbolJobs(tui::RecorderTuiPreset& preset, std::string_view input) {
    GeneratedJobsAppendResult result{};
    tui::SymbolBatchInput batch{};
    std::string error;
    if (!tui::loadSymbolBatchInput(input, tui::symbolListConfigDir(), batch, error)) {
        result.error = error;
        return result;
    }

    result.symbols = batch.symbols.size();
    result.loadedFiles = std::move(batch.loadedFiles);
    const auto generated = tui::generateJobsForSymbols(batch.symbols, tui::allCryptoVenueSpecs(), preset.jobs.size());
    for (const auto& job : generated) {
        if (containsCaptureJob(preset.jobs, job)) {
            ++result.skipped;
            continue;
        }
        preset.jobs.push_back(job);
        ++result.added;
    }
    return result;
}

std::string generatedJobsMessage(const GeneratedJobsAppendResult& result, std::string_view action) {
    if (!result.error.empty()) return result.error;
    std::ostringstream out;
    out << action << ' ' << result.added << " job(s) from " << result.symbols << " symbol(s)";
    if (result.skipped != 0u) out << ", skipped " << result.skipped << " duplicate(s)";
    if (!result.loadedFiles.empty()) out << ", loaded " << result.loadedFiles.front().string();
    return out.str();
}

void toggleChannelByIndex(tui::ChannelSelection& channels, int index) {
    switch (index) {
        case 0: channels.trades = !channels.trades; break;
        case 1: channels.liquidations = !channels.liquidations; break;
        case 2: channels.bookTicker = !channels.bookTicker; break;
        case 3: channels.orderbook = !channels.orderbook; break;
        case 4: channels.markPrice = !channels.markPrice; break;
        case 5: channels.indexPrice = !channels.indexPrice; break;
        case 6: channels.funding = !channels.funding; break;
        case 7: channels.priceLimit = !channels.priceLimit; break;
        default: break;
    }
    if (!tui::anyChannelSelected(channels)) channels.trades = true;
}

bool channelByIndex(const tui::ChannelSelection& channels, int index) {
    switch (index) {
        case 0: return channels.trades;
        case 1: return channels.liquidations;
        case 2: return channels.bookTicker;
        case 3: return channels.orderbook;
        case 4: return channels.markPrice;
        case 5: return channels.indexPrice;
        case 6: return channels.funding;
        case 7: return channels.priceLimit;
        default: return false;
    }
}

const char* channelNameByIndex(int index) {
    switch (index) {
        case 0: return "trades";
        case 1: return "liquidations";
        case 2: return "bookticker";
        case 3: return "orderbook";
        case 4: return "mark_price";
        case 5: return "index_price";
        case 6: return "funding";
        case 7: return "price_limit";
        default: return "";
    }
}

capture::CaptureChannel captureChannelForLaunch(tui::LaunchChannel channel) noexcept {
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

struct TuiChannelAvailabilityCacheEntry {
    std::string exchange;
    std::string market;
    std::string symbol;
    std::uint8_t apiSlot{1u};
    tui::LaunchChannel channel{tui::LaunchChannel::Trades};
    bool available{false};
};

struct TuiChannelAvailabilityCache {
    std::vector<TuiChannelAvailabilityCacheEntry> entries{};
};

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
    const std::string symbol = lower(job.symbol);
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

void renderEditJob(const tui::RecorderTuiJob& job, int row, std::string_view message) {
    const auto viewport = currentViewport();
    clearScreen();
    printLine("hft-recorder TUI / edit job", viewport);
    std::putchar('\n');
    const std::string durationText = job.durationMin == 0 ? "until stop" : std::to_string(job.durationMin) + "m";
    const char* marker = row == 0 ? ">" : " ";
    printLine(std::string(marker) + " symbol       " + job.symbol, viewport);
    marker = row == 1 ? ">" : " ";
    printLine(std::string(marker) + " name         " + job.name, viewport);
    marker = row == 2 ? ">" : " ";
    printLine(std::string(marker) + " exchange     " + job.exchange, viewport);
    marker = row == 3 ? ">" : " ";
    printLine(std::string(marker) + " market       " + job.market, viewport);
    marker = row == 4 ? ">" : " ";
    printLine(std::string(marker) + " duration     " + durationText, viewport);
    for (int i = 0; i < 8; ++i) {
        marker = row == i + 5 ? ">" : " ";
        printLine(std::string(marker) + " [" + (channelByIndex(job.channels, i) ? "x" : " ") + "] " +
                      channelNameByIndex(i),
                  viewport);
    }
    std::putchar('\n');
    printLine("Enter edit/toggle | arrows move | Esc save/back", viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

void editJob(TerminalGuard& terminal, tui::RecorderTuiJob& job) {
    int row = 0;
    std::string message;
    while (!gInterrupted) {
        renderEditJob(job, row, message);
        message.clear();
        const Key key = readKey(250);
        if (key.kind == KeyKind::Up) row = std::max(0, row - 1);
        else if (key.kind == KeyKind::Down) row = std::min(12, row + 1);
        else if (key.kind == KeyKind::Escape) return;
        else if (key.kind == KeyKind::Enter || (key.kind == KeyKind::Character && key.ch == ' ')) {
            if (row == 0) job.symbol = promptLine(terminal, "symbol", job.symbol);
            else if (row == 1) job.name = promptLine(terminal, "name", job.name);
            else if (row == 2) job.exchange = lower(promptLine(terminal, "exchange", job.exchange));
            else if (row == 3) job.market = lower(promptLine(terminal, "market", job.market));
            else if (row == 4) {
                const std::string value = promptLine(terminal, "duration minutes (0/none = until stop)",
                                                     job.durationMin == 0 ? "0" : std::to_string(job.durationMin));
                std::string error;
                std::int64_t minutes = 0;
                if (tui::parseDurationMinutes(value, minutes, error)) job.durationMin = minutes;
                else message = error;
            } else {
                toggleChannelByIndex(job.channels, row - 5);
            }
        }
    }
}

void renderMainMenu(const tui::RecorderTuiPreset& preset, std::size_t selected, const std::filesystem::path& presetPath, std::string_view message) {
    const auto viewport = currentViewport();
    clearScreen();
    printLine("hft-recorder TUI", viewport);
    printLine("output: " + preset.outputDir.string() + " | preset: " + presetPath.string() +
                  " | progress: " + std::to_string(preset.progressSec) + "s",
              viewport);
    std::putchar('\n');
    if (preset.jobs.empty()) {
        printLine("No jobs. Press 'a' to add one.", viewport);
    } else {
        std::vector<std::string> jobLines;
        jobLines.reserve(preset.jobs.size());
        for (std::size_t i = 0; i < preset.jobs.size(); ++i) {
            std::ostringstream line;
            line << (i == selected ? '>' : ' ') << ' ' << (i + 1u) << "  " << jobLabel(preset.jobs[i]);
            jobLines.push_back(line.str());
        }
        const int reserved = message.empty() ? 6 : 7;
        for (const auto& line : tui::limitLinesForViewport(jobLines, viewport, reserved)) {
            printLine(line, viewport);
        }
    }
    std::putchar('\n');
    printLine("[a] add  [g] gen symbols  [Enter] edit  [c] copy  [d] delete  [w] save  [s] save as  [l] load  [r] start  [q] quit",
              viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

struct RunOutputGroups {
    std::filesystem::path root{};
    std::int64_t timestampNs{0};
    std::map<std::string, std::filesystem::path> bySymbol{};
};

RunOutputGroups makeRunOutputGroups(const tui::RecorderTuiPreset& preset) {
    return RunOutputGroups{.root = preset.outputDir, .timestampNs = wallNowNs()};
}

std::filesystem::path outputDirForRunJob(RunOutputGroups& groups, const tui::RecorderTuiJob& job) {
    std::string normalizedSymbol = recordings::normalizeRecordingSymbol(job.symbol);
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

struct RunningJob {
    tui::RecorderTuiJob job{};
    capture::CaptureConfig config{};
    std::unique_ptr<capture::CaptureCoordinator> coordinator{};
    std::future<std::shared_ptr<RunningJob>> startFuture{};
    Clock::time_point scheduledStart{};
    Clock::time_point started{};
    tui::ChannelSelection skippedChannels{};
    bool startInProgress{false};
    bool launched{false};
    bool running{false};
    bool stopRequested{false};
    bool finalized{false};
    std::string status{"idle"};
    std::string planNote{};
    std::string error{};
};

constexpr std::int64_t kStartSlotGraceSec = 15;
constexpr auto kDeadZeroRowSessionTtl = std::chrono::minutes(5);
constexpr auto kDeadZeroRowSweepInterval = std::chrono::seconds(30);

std::int64_t startAgeSeconds(const RunningJob& job, Clock::time_point now) noexcept {
    if (!job.startInProgress) return 0;
    return std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count());
}

bool startStalled(const RunningJob& job, Clock::time_point now) noexcept {
    return job.startInProgress && startAgeSeconds(job, now) >= kStartSlotGraceSec;
}

bool manifestHasRows(const capture::SessionManifest& manifest) noexcept {
    return manifest.tradesCount != 0u
        || manifest.liquidationsCount != 0u
        || manifest.bookTickerCount != 0u
        || manifest.markPriceCount != 0u
        || manifest.indexPriceCount != 0u
        || manifest.fundingCount != 0u
        || manifest.priceLimitCount != 0u
        || manifest.depthCount != 0u
        || manifest.candlesCount != 0u
        || manifest.candles2Count != 0u;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string skippedChannelsNote(const tui::ChannelSelection& channels) {
    if (!tui::anyChannelSelected(channels)) return {};
    return "skipped channels: " + tui::renderChannelSelection(channels);
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

bool anyRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesRunning() || coordinator.liquidationsRunning() || coordinator.bookTickerRunning() ||
           coordinator.orderbookRunning() || coordinator.markPriceRunning() || coordinator.indexPriceRunning() ||
           coordinator.fundingRunning() || coordinator.priceLimitRunning();
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
    started.skippedChannels = job.skippedChannels;
    started.planNote = job.planNote;
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

struct DeadSessionSweepResult {
    int removedSessions{0};
    int removedGroups{0};
};

bool looksLikeSessionDirName(std::string_view name) noexcept {
    std::size_t digits = 0;
    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))) {
        ++digits;
    }
    return digits >= 10u && digits < name.size() && name[digits] == '_';
}

bool sessionDirHasJsonlRows(const std::filesystem::path& sessionDir) {
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(sessionDir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".jsonl") continue;
        if (std::filesystem::file_size(it->path(), ec) != 0u && !ec) return true;
        ec.clear();
    }
    return false;
}

bool deadZeroRowManifest(const capture::SessionManifest& manifest, std::int64_t nowNs) noexcept {
    if (manifest.startedAtNs <= 0) return false;
    if (manifestHasRows(manifest)) return false;
    const auto ttlNs = std::chrono::duration_cast<std::chrono::nanoseconds>(kDeadZeroRowSessionTtl).count();
    return nowNs - manifest.startedAtNs >= ttlNs;
}

bool removeOrphanSessionDirIfDead(const std::filesystem::path& sessionDir,
                                  std::filesystem::file_time_type now,
                                  DeadSessionSweepResult& result) {
    if (!looksLikeSessionDirName(sessionDir.filename().string())) return false;
    std::error_code ec;
    if (std::filesystem::exists(sessionDir / "manifest.json", ec) || ec) return false;

    const auto updatedAt = std::filesystem::last_write_time(sessionDir, ec);
    if (ec || now - updatedAt < kDeadZeroRowSessionTtl) return false;
    if (sessionDirHasJsonlRows(sessionDir)) return false;

    std::filesystem::remove_all(sessionDir, ec);
    if (ec) return false;
    ++result.removedSessions;
    return true;
}

bool removeSessionDirIfDeadZeroRows(const std::filesystem::path& sessionDir,
                                    std::int64_t nowNs,
                                    DeadSessionSweepResult& result) {
    const std::filesystem::path manifestPath = sessionDir / "manifest.json";
    const std::string document = readTextFile(manifestPath);
    if (document.empty()) return false;

    capture::SessionManifest manifest{};
    if (!isOk(capture::parseManifestJson(document, manifest))) return false;
    if (!deadZeroRowManifest(manifest, nowNs)) return false;

    std::error_code ec;
    std::filesystem::remove_all(sessionDir, ec);
    if (ec) return false;
    ++result.removedSessions;
    return true;
}

void removeGroupIfEmpty(const std::filesystem::path& root,
                        const std::filesystem::path& groupPath,
                        DeadSessionSweepResult& result) {
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return;
    const auto canonicalGroup = std::filesystem::weakly_canonical(groupPath, ec);
    if (ec || canonicalGroup == canonicalRoot) return;
    if (!std::filesystem::exists(canonicalGroup, ec) || ec) return;
    if (!std::filesystem::is_empty(canonicalGroup, ec) || ec) return;
    std::filesystem::remove(canonicalGroup, ec);
    if (!ec) ++result.removedGroups;
}

DeadSessionSweepResult sweepDeadZeroRowSessionsInGroup(const std::filesystem::path& root,
                                                       const std::filesystem::path& groupPath,
                                                       std::int64_t nowNs) {
    DeadSessionSweepResult result{};
    std::error_code ec;
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    if (!std::filesystem::exists(groupPath, ec) || ec) return result;
    for (std::filesystem::directory_iterator it(groupPath, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        if (removeOrphanSessionDirIfDead(it->path(), fileNow, result)) continue;
        (void)removeSessionDirIfDeadZeroRows(it->path(), nowNs, result);
    }
    removeGroupIfEmpty(root, groupPath, result);
    return result;
}

DeadSessionSweepResult sweepDeadZeroRowSessions(const std::filesystem::path& root, std::int64_t nowNs) {
    DeadSessionSweepResult result{};
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) return result;
    std::vector<std::filesystem::path> sessionDirs;
    std::vector<std::filesystem::path> orphanDirs;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory(ec) && looksLikeSessionDirName(it->path().filename().string())) {
            orphanDirs.push_back(it->path());
            continue;
        }
        if (!it->is_regular_file(ec) || it->path().filename() != "manifest.json") continue;
        sessionDirs.push_back(it->path().parent_path());
    }
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    for (const auto& orphanDir : orphanDirs) {
        const std::filesystem::path groupPath = orphanDir.parent_path();
        if (removeOrphanSessionDirIfDead(orphanDir, fileNow, result)) {
            removeGroupIfEmpty(root, groupPath, result);
        }
    }
    for (const auto& sessionDir : sessionDirs) {
        const std::filesystem::path groupPath = sessionDir.parent_path();
        if (removeSessionDirIfDeadZeroRows(sessionDir, nowNs, result)) {
            removeGroupIfEmpty(root, groupPath, result);
        }
    }
    return result;
}

DeadSessionSweepResult sweepRunOutputGroups(const RunOutputGroups& outputGroups) {
    DeadSessionSweepResult total{};
    const std::int64_t nowNs = wallNowNs();
    for (const auto& [_, groupPath] : outputGroups.bySymbol) {
        const DeadSessionSweepResult result =
            sweepDeadZeroRowSessionsInGroup(outputGroups.root, groupPath, nowNs);
        total.removedSessions += result.removedSessions;
        total.removedGroups += result.removedGroups;
    }
    return total;
}

std::string deadSessionSweepMessage(const DeadSessionSweepResult& result) {
    if (result.removedSessions == 0 && result.removedGroups == 0) return {};
    std::ostringstream out;
    out << "removed " << result.removedSessions << " dead zero-row session(s)";
    if (result.removedGroups != 0) out << ", " << result.removedGroups << " empty group(s)";
    return out.str();
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

std::uint64_t totalRows(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesCount() + coordinator.liquidationsCount() + coordinator.bookTickerCount() +
           coordinator.depthCount() + coordinator.markPriceCount() + coordinator.indexPriceCount() +
           coordinator.fundingCount() + coordinator.priceLimitCount();
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

void renderRunning(const std::vector<RunningJob>& jobs, std::size_t selected, std::string_view message) {
    const auto viewport = currentViewport();
    const auto now = Clock::now();
    clearScreen();
    std::uint64_t aggregateRows = 0;
    int runningCount = 0;
    int startingCount = 0;
    int stalledCount = 0;
    int pendingCount = 0;
    int skippedCount = 0;
    int errorCount = 0;
    for (const auto& job : jobs) {
        if (job.coordinator) aggregateRows += totalRows(*job.coordinator);
        if (job.running) ++runningCount;
        if (job.startInProgress) ++startingCount;
        if (startStalled(job, now)) ++stalledCount;
        if (!job.launched && !job.finalized && !job.startInProgress) ++pendingCount;
        if (job.status == "skipped") ++skippedCount;
        if (job.status == "error") ++errorCount;
    }
    printLine("hft-recorder TUI / running  jobs=" + std::to_string(jobs.size()) +
                  " running=" + std::to_string(runningCount) +
                  " starting=" + std::to_string(startingCount) +
                  " stalled=" + std::to_string(stalledCount) +
                  " pending=" + std::to_string(pendingCount) +
                  " skipped=" + std::to_string(skippedCount) +
                  " errors=" + std::to_string(errorCount) +
                  " rows=" + std::to_string(static_cast<unsigned long long>(aggregateRows)),
              viewport);
    std::putchar('\n');

    std::vector<std::string> lines;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        const auto durationSec = job.job.durationMin > 0 ? job.job.durationMin * 60 : 0;
        std::ostringstream head;
        head << (i == selected ? '>' : ' ') << ' ' << (i + 1u) << ' ' << job.job.name << ' ' << job.status << ' '
             << job.job.exchange << '/' << job.job.market << ' ' << job.job.symbol;
        if (job.startInProgress) {
            head << " starting_for=" << static_cast<long long>(startAgeSeconds(job, now)) << 's';
            if (startStalled(job, now)) head << " stalled";
        } else if (!job.launched && !job.finalized) {
            const auto waitMs = std::max<std::int64_t>(
                0, std::chrono::duration_cast<std::chrono::milliseconds>(job.scheduledStart - now).count());
            head << " starts_in=" << static_cast<long long>(waitMs) << "ms";
        } else if (job.launched) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - job.started).count();
            head << " elapsed=" << static_cast<long long>(elapsed) << 's';
            if (durationSec > 0) head << '/' << static_cast<long long>(durationSec) << 's';
        }
        if (job.coordinator) {
            head << " session="
                 << tui::compactSessionPath(job.coordinator->sessionDirCopy(), std::max(16, viewport.cols / 2));
        }
        lines.push_back(head.str());
        if (!job.planNote.empty()) lines.push_back("      note: " + job.planNote);
        if (job.coordinator) {
            std::ostringstream counts;
            counts << "      trades=" << static_cast<unsigned long long>(job.coordinator->tradesCount())
                   << " liq=" << static_cast<unsigned long long>(job.coordinator->liquidationsCount())
                   << " bbo=" << static_cast<unsigned long long>(job.coordinator->bookTickerCount())
                   << " depth=" << static_cast<unsigned long long>(job.coordinator->depthCount())
                   << " mark=" << static_cast<unsigned long long>(job.coordinator->markPriceCount())
                   << " index=" << static_cast<unsigned long long>(job.coordinator->indexPriceCount())
                   << " funding=" << static_cast<unsigned long long>(job.coordinator->fundingCount())
                   << " limits=" << static_cast<unsigned long long>(job.coordinator->priceLimitCount());
            lines.push_back(counts.str());
        }
        const std::string last = job.coordinator ? job.coordinator->lastError() : std::string{};
        const std::string error = !job.error.empty() ? job.error : last;
        if (!error.empty()) lines.push_back("      warn/error: " + error);
    }
    const int reserved = message.empty() ? 5 : 6;
    for (const auto& line : tui::limitLinesForViewport(lines, viewport, reserved)) {
        printLine(line, viewport);
    }
    std::putchar('\n');
    printLine("[+] add symbols  [s/c] stop selected  [a/q] stop all and return", viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
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
    std::string lastIdleMessage;
    while (true) {
        if (gInterrupted) {
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

    writeRunGroupManifests(runPreset.outputDir, outputGroups);
}

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder tui [--preset path] [--output-dir path] [--progress-sec n]");
}

}  // namespace

int runTui(int argc, char** argv) {
    tui::RecorderTuiPreset preset{};
    std::filesystem::path presetPath = tui::defaultPresetPath();
    bool explicitPreset = false;
    bool explicitOutputDir = false;
    bool explicitProgressSec = false;
    std::filesystem::path outputDirOverride;
    int progressSecOverride = preset.progressSec;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--preset") {
            if (i + 1 >= argc) {
                std::fputs("tui: --preset requires a path\n", stderr);
                return 2;
            }
            presetPath = tui::resolvePresetPath(argv[++i]);
            explicitPreset = true;
            continue;
        }
        if (arg == "--output-dir") {
            if (i + 1 >= argc) {
                std::fputs("tui: --output-dir requires a path\n", stderr);
                return 2;
            }
            outputDirOverride = argv[++i];
            explicitOutputDir = true;
            continue;
        }
        if (arg == "--progress-sec") {
            if (i + 1 >= argc) {
                std::fputs("tui: --progress-sec requires a value\n", stderr);
                return 2;
            }
            progressSecOverride = std::max(1, std::atoi(argv[++i]));
            explicitProgressSec = true;
            continue;
        }
        std::fprintf(stderr, "tui: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        printUsage();
        return 2;
    }

    std::string message;
    if (std::filesystem::exists(presetPath)) {
        if (!tui::loadPresetFile(presetPath, preset, message)) {
            if (explicitPreset) {
                std::fprintf(stderr, "tui: %s\n", message.c_str());
                return 1;
            }
            message = "default preset ignored: " + message;
        } else {
            message = "loaded " + presetPath.string();
        }
    }

    if (explicitOutputDir) preset.outputDir = outputDirOverride;
    if (explicitProgressSec) preset.progressSec = progressSecOverride;
    if (preset.jobs.empty()) addDefaultJob(preset);

    TerminalGuard terminal;
    if (!terminal.interactive()) {
        std::fputs("tui: interactive terminal is required\n", stderr);
        return 2;
    }
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::size_t selected = 0;
    bool dirty = true;
    while (!gInterrupted) {
        if (selected >= preset.jobs.size()) {
            selected = preset.jobs.empty() ? 0 : preset.jobs.size() - 1u;
            dirty = true;
        }
        if (dirty) {
            renderMainMenu(preset, selected, presetPath, message);
            message.clear();
            dirty = false;
        }
        const Key key = readKey(250);
        if (key.kind == KeyKind::None) continue;
        if (key.kind == KeyKind::Up && selected > 0) {
            --selected;
            dirty = true;
        } else if (key.kind == KeyKind::Down && selected + 1u < preset.jobs.size()) {
            ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'a') {
            addDefaultJob(preset);
            selected = preset.jobs.size() - 1u;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'g') {
            const std::string input = promptLine(terminal, "symbols or .ini list");
            if (input.empty()) {
                message = "symbol generation canceled";
                dirty = true;
                continue;
            }
            const std::size_t before = preset.jobs.size();
            const GeneratedJobsAppendResult result = appendGeneratedSymbolJobs(preset, input);
            if (preset.jobs.size() > before) selected = before;
            message = generatedJobsMessage(result, "added");
            dirty = true;
        } else if (key.kind == KeyKind::Enter) {
            if (!preset.jobs.empty()) editJob(terminal, preset.jobs[selected]);
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'c') {
            duplicateJob(preset, selected);
            if (!preset.jobs.empty()) ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'd') {
            if (!preset.jobs.empty()) preset.jobs.erase(preset.jobs.begin() + static_cast<std::ptrdiff_t>(selected));
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'w' || key.ch == 'W')) {
            std::string error;
            message = tui::savePresetFile(presetPath, preset, error) ? "saved " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 's') {
            const std::string maybePath = promptLine(terminal, "save preset as path");
            if (maybePath.empty()) {
                message = "save as canceled";
            } else {
                const std::filesystem::path savePath = tui::resolvePresetPath(maybePath);
                std::string error;
                if (tui::savePresetFile(savePath, preset, error)) {
                    presetPath = savePath;
                    message = "saved " + presetPath.string();
                } else {
                    message = error;
                }
            }
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'l') {
            const std::string maybePath = promptLine(terminal, "load preset path", presetPath.string());
            if (!maybePath.empty()) presetPath = tui::resolvePresetPath(maybePath);
            std::string error;
            message = tui::loadPresetFile(presetPath, preset, error) ? "loaded " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'r') {
            if (preset.jobs.empty()) {
                message = "add at least one job";
                dirty = true;
            } else if (preset.jobs.size() > static_cast<std::size_t>(std::max(1, preset.maxActiveJobs))) {
                (void)runShardPresetInteractive(preset, presetPath);
                dirty = true;
            } else {
                runJobs(terminal, preset);
                dirty = true;
            }
        } else if (key.kind == KeyKind::Character && key.ch == 'q') {
            break;
        }
    }
    return 0;
}

}  // namespace hftrec::app
