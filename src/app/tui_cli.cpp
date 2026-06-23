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
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/tui/RecorderTuiPreset.hpp"
#include "core/tui/TerminalRender.hpp"

namespace hftrec::app {

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
    printLine("[a] add  [e/Enter] edit  [c] copy  [d] delete  [o] output  [p] progress  [w] save  [s] save as  [l] load  [r/R] start all  [q] quit",
              viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

capture::CaptureConfig makeCaptureConfig(const tui::RecorderTuiPreset& preset, const tui::RecorderTuiJob& job) {
    capture::CaptureConfig config{};
    config.exchange = job.exchange;
    config.market = job.market;
    config.symbols = {job.symbol};
    config.outputDir = preset.outputDir;
    config.durationSec = job.durationMin > 0 ? job.durationMin * 60 : 0;
    config.snapshotIntervalSec = 60;
    config.liveCacheMode = capture::LiveCacheMode::Off;
    return config;
}

tui::RecorderTuiPreset presetForRunGroup(const tui::RecorderTuiPreset& preset) {
    tui::RecorderTuiPreset runPreset = preset;
    std::string normalizedSymbol = "UNKNOWN";
    for (const auto& job : preset.jobs) {
        const std::string symbol = recordings::normalizeRecordingSymbol(job.symbol);
        if (!symbol.empty() && symbol != "UNKNOWN") {
            normalizedSymbol = symbol;
            break;
        }
    }
    const std::string groupName = recordings::recordingGroupFolderName(wallNowNs(), normalizedSymbol);
    runPreset.outputDir = uniquePath(preset.outputDir, groupName);
    return runPreset;
}

struct RunningJob {
    tui::RecorderTuiJob job{};
    capture::CaptureConfig config{};
    std::unique_ptr<capture::CaptureCoordinator> coordinator{};
    Clock::time_point started{};
    bool running{false};
    bool stopRequested{false};
    bool finalized{false};
    std::string status{"idle"};
    std::string error{};
};

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

void requestStopJob(RunningJob& job) {
    if (!job.coordinator || job.finalized || job.stopRequested) return;
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

RunningJob startJob(const tui::RecorderTuiPreset& preset, const tui::RecorderTuiJob& source) {
    RunningJob job{};
    job.job = source;
    job.config = makeCaptureConfig(preset, source);
    job.coordinator = std::make_unique<capture::CaptureCoordinator>();
    job.started = Clock::now();
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

void finalizeJob(RunningJob& job) {
    if (!job.coordinator || job.finalized) return;
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

void renderRunning(const std::vector<RunningJob>& jobs, std::size_t selected, std::string_view message) {
    const auto viewport = currentViewport();
    clearScreen();
    std::uint64_t aggregateRows = 0;
    int runningCount = 0;
    for (const auto& job : jobs) {
        if (job.coordinator) aggregateRows += totalRows(*job.coordinator);
        if (job.running) ++runningCount;
    }
    printLine("hft-recorder TUI / running  jobs=" + std::to_string(jobs.size()) +
                  " running=" + std::to_string(runningCount) +
                  " rows=" + std::to_string(static_cast<unsigned long long>(aggregateRows)),
              viewport);
    std::putchar('\n');

    std::vector<std::string> lines;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - job.started).count();
        const auto durationSec = job.job.durationMin > 0 ? job.job.durationMin * 60 : 0;
        std::ostringstream head;
        head << (i == selected ? '>' : ' ') << ' ' << (i + 1u) << ' ' << job.job.name << ' ' << job.status << ' '
             << job.job.exchange << '/' << job.job.market << ' ' << job.job.symbol << " elapsed="
             << static_cast<long long>(elapsed) << 's';
        if (durationSec > 0) head << '/' << static_cast<long long>(durationSec) << 's';
        if (job.coordinator) {
            head << " session="
                 << tui::compactSessionPath(job.coordinator->sessionDirCopy(), std::max(16, viewport.cols / 2));
        }
        lines.push_back(head.str());
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
    printLine("[s/c] stop selected  [a/q] stop all and return", viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

void runJobs(TerminalGuard&, const tui::RecorderTuiPreset& preset) {
    const tui::RecorderTuiPreset runPreset = presetForRunGroup(preset);
    std::vector<RunningJob> jobs;
    jobs.reserve(runPreset.jobs.size());
    for (const auto& source : runPreset.jobs) jobs.push_back(startJob(runPreset, source));

    std::size_t selected = 0;
    std::string message;
    auto nextProgress = Clock::now();
    bool dirty = true;
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
            if (job.stopRequested && !job.finalized && job.coordinator && !anyRunningChannel(*job.coordinator)) {
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
        const bool anyPending = std::any_of(jobs.begin(), jobs.end(), [](const RunningJob& job) { return !job.finalized; });
        std::string idleMessage;
        if (!anyLive && anyPending) {
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
    for (auto& job : jobs) finalizeJob(job);

    std::error_code ec;
    const auto targetGroup = std::filesystem::weakly_canonical(runPreset.outputDir, ec);
    const auto discovery = recordings::discoverRecordings(preset.outputDir);
    for (const auto& group : discovery.groups) {
        if (group.path == targetGroup) {
            std::string error;
            (void)recordings::writeGroupManifest(group, &error);
            break;
        }
    }
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
        } else if ((key.kind == KeyKind::Character && key.ch == 'e') || key.kind == KeyKind::Enter) {
            if (!preset.jobs.empty()) editJob(terminal, preset.jobs[selected]);
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'c') {
            duplicateJob(preset, selected);
            if (!preset.jobs.empty()) ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'd') {
            if (!preset.jobs.empty()) preset.jobs.erase(preset.jobs.begin() + static_cast<std::ptrdiff_t>(selected));
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'o') {
            preset.outputDir = promptLine(terminal, "output directory", preset.outputDir.string());
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'p') {
            const std::string value = promptLine(terminal, "progress seconds", std::to_string(preset.progressSec));
            preset.progressSec = std::max(1, std::atoi(value.c_str()));
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'w' || key.ch == 'W')) {
            std::string error;
            message = tui::savePresetFile(presetPath, preset, error) ? "saved " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 's' || key.ch == 'S')) {
            const std::string maybePath = promptLine(terminal, "save as new preset path");
            if (maybePath.empty()) {
                message = "save as canceled";
            } else {
                const std::filesystem::path saveAsPath = tui::resolvePresetPath(maybePath);
                if (std::filesystem::exists(saveAsPath)) {
                    message = "save as refused: target exists " + saveAsPath.string();
                } else {
                    std::string error;
                    if (tui::savePresetFile(saveAsPath, preset, error)) {
                        presetPath = saveAsPath;
                        message = "saved new preset " + presetPath.string();
                    } else {
                        message = error;
                    }
                }
            }
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'l') {
            const std::string maybePath = promptLine(terminal, "load preset path", presetPath.string());
            if (!maybePath.empty()) presetPath = tui::resolvePresetPath(maybePath);
            std::string error;
            message = tui::loadPresetFile(presetPath, preset, error) ? "loaded " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'r' || key.ch == 'R')) {
            if (preset.jobs.empty()) {
                message = "add at least one job";
                dirty = true;
            } else if (tui::requiresHeavyRunConfirmation(preset) && key.ch != 'R') {
                message = tui::heavyRunWarning(preset);
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
