#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "Tui/RecorderTuiLaunch.hpp"
#include "Tui/RecorderTuiPreset.hpp"
#include "Tui/RecorderTuiShard.hpp"
#include "Tui/Status/RecorderTuiShardStatus.hpp"
#include "Tui/TerminalRender.hpp"

namespace hftrec::app {

namespace {

using Clock = std::chrono::steady_clock;

struct ShardProcess {
    int index{0};
    std::string label{};
    std::filesystem::path presetPath;
    std::filesystem::path statusPath;
    std::filesystem::path logPath;
    std::filesystem::path outputGroupMapPath;
    pid_t pid{-1};
    bool outputIsGroup{false};
    bool launchStarted{false};
    bool stopRequested{false};
    bool exited{false};
    int exitStatus{0};
    std::uint64_t rssKb{0};
    std::vector<std::string> exclusiveMarketDataSessionKeys{};
    tui::RecorderTuiShardStatus status{};
};

struct ShardRenderSummary {
    std::uint64_t rows{0};
    std::uint64_t rssKb{0};
    int running{0};
    int starting{0};
    int stalled{0};
    int pending{0};
    int skipped{0};
    int cleanExited{0};
    int failedExited{0};
    int errors{0};
    int active{0};
    int queued{0};
    int completed{0};
};

class ShardTerminalGuard {
  public:
    ShardTerminalGuard() {
        interactive_ = ::isatty(STDIN_FILENO) == 1;
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
        (void)::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_);
    }

    ~ShardTerminalGuard() {
        if (interactive_) (void)::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }

  private:
    bool interactive_{false};
    termios original_{};
    termios raw_{};
};

std::int64_t wallNowNs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

tui::TerminalViewport currentViewport() noexcept {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0 && size.ws_col > 0) {
        return tui::sanitizeViewport(
            tui::TerminalViewport{.rows = static_cast<int>(size.ws_row), .cols = static_cast<int>(size.ws_col)});
    }
    return tui::sanitizeViewport({});
}

void clearScreen() {
    std::fputs("\033[2J\033[H", stdout);
}

void printLine(std::string_view line, tui::TerminalViewport viewport) {
    const std::string text = tui::truncateForTerminal(line, viewport.cols);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
}

std::filesystem::path selfExecutablePath() {
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !path.empty()) return path;
    return "hft-recorder";
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

std::string normalizedRecordingSymbolForJob(const tui::RecorderTuiJob& job) {
    std::string symbol = recordings::recordingFolderSymbol(job.exchange, job.market, job.symbol);
    return symbol.empty() ? std::string{"UNKNOWN"} : symbol;
}

std::vector<std::string> normalizedSymbolsForPreset(const tui::RecorderTuiPreset& preset) {
    std::vector<std::string> symbols;
    symbols.reserve(preset.jobs.size());
    for (const auto& job : preset.jobs) {
        const std::string symbol = normalizedRecordingSymbolForJob(job);
        const auto exists = std::find(symbols.begin(), symbols.end(), symbol) != symbols.end();
        if (!exists) symbols.push_back(symbol);
    }
    return symbols;
}

std::vector<std::string> normalizedVenuesForPreset(const tui::RecorderTuiPreset& preset) {
    std::vector<std::string> venues;
    venues.reserve(preset.jobs.size());
    for (const auto& job : preset.jobs) {
        const std::string key = job.exchange + "|" + job.market;
        if (std::find(venues.begin(), venues.end(), key) == venues.end()) venues.push_back(key);
    }
    return venues;
}

std::string shardLabelForPreset(const tui::RecorderTuiPreset& preset,
                                tui::RecorderTuiShardGrouping grouping) {
    if (preset.jobs.empty()) return "empty";
    if (grouping == tui::RecorderTuiShardGrouping::ByVenue) {
        const auto& job = preset.jobs.front();
        return job.exchange + "/" + job.market + " " + std::to_string(normalizedSymbolsForPreset(preset).size()) + " symbols";
    }
    if (grouping == tui::RecorderTuiShardGrouping::ByJob && preset.jobs.size() == 1u) {
        const auto& job = preset.jobs.front();
        if (!job.name.empty()) return job.name;
        return normalizedRecordingSymbolForJob(job);
    }

    const auto symbols = normalizedSymbolsForPreset(preset);
    if (symbols.empty()) return "empty";
    if (symbols.size() == 1u) return symbols.front();
    return symbols.front() + "+" + std::to_string(symbols.size() - 1u) + " symbols";
}

int defaultShardCount(const tui::RecorderTuiPreset& preset, tui::RecorderTuiShardGrouping grouping) {
    if (preset.jobs.empty()) return 1;
    if (grouping == tui::RecorderTuiShardGrouping::ByJob) {
        return std::max(1, static_cast<int>(preset.jobs.size()));
    }
    if (grouping == tui::RecorderTuiShardGrouping::ByVenue) {
        return std::max(1, static_cast<int>(normalizedVenuesForPreset(preset).size()));
    }
    const auto symbols = normalizedSymbolsForPreset(preset);
    return std::max(1, static_cast<int>(symbols.size()));
}

int positiveIntFromText(const char* text) noexcept {
    if (text == nullptr || *text == '\0') return 0;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || value <= 0) return 0;
    return static_cast<int>(std::min<long>(value, 1'000'000L));
}

int configuredMaxActiveShards(const tui::RecorderTuiPreset& preset,
                              tui::RecorderTuiShardGrouping grouping,
                              int shardCount,
                              int requestedMaxActiveShards) {
    if (requestedMaxActiveShards > 0) {
        return tui::clampRecorderTuiMaxActiveShards(requestedMaxActiveShards, shardCount);
    }
    if (const int envValue = positiveIntFromText(std::getenv("HFTREC_MAX_ACTIVE_SHARDS")); envValue > 0) {
        return tui::clampRecorderTuiMaxActiveShards(envValue, shardCount);
    }
    return tui::defaultRecorderTuiMaxActiveShards(preset, grouping, shardCount);
}

std::vector<std::string> exclusiveMarketDataSessionKeysForPreset(const tui::RecorderTuiPreset& preset) {
    std::vector<std::string> keys;
    for (const auto& job : preset.jobs) {
        std::string key = tui::exclusiveMarketDataSessionKey(job);
        if (key.empty()) continue;
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        keys.push_back(std::move(key));
    }
    return keys;
}

bool exclusiveMarketDataSessionConflict(const ShardProcess& candidate,
                                        const std::vector<ShardProcess>& shards) {
    if (candidate.exclusiveMarketDataSessionKeys.empty()) return false;
    for (const auto& shard : shards) {
        if (!shard.launchStarted || shard.exited || shard.stopRequested) continue;
        for (const auto& candidateKey : candidate.exclusiveMarketDataSessionKeys) {
            if (std::find(shard.exclusiveMarketDataSessionKeys.begin(),
                          shard.exclusiveMarketDataSessionKeys.end(),
                          candidateKey) != shard.exclusiveMarketDataSessionKeys.end()) {
                return true;
            }
        }
    }
    return false;
}

std::filesystem::path shardRunRoot(const tui::RecorderTuiPreset& preset) {
    std::ostringstream name;
    name << wallNowNs();
    return preset.outputDir / ".shards" / name.str();
}

using OutputGroupMap = std::map<std::string, std::filesystem::path>;

bool writeOutputGroupMap(const std::filesystem::path& path,
                         const OutputGroupMap& groups,
                         std::string& error) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::out | std::ios::trunc);
    if (!out) {
        error = "failed to create output group map: " + temporary.string();
        return false;
    }
    for (const auto& [symbol, groupPath] : groups) out << symbol << '\t' << groupPath.string() << '\n';
    out.close();
    if (!out) {
        error = "failed to write output group map: " + temporary.string();
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec) {
        error = "failed to publish output group map: " + ec.message();
        return false;
    }
    return true;
}

void writeSupervisorGroupManifests(const std::filesystem::path& recordingsRoot,
                                   const OutputGroupMap& groups) {
    for (const auto& [_, groupPath] : groups) {
        std::string ignoredError;
        (void)recordings::writeGroupManifestForPath(recordingsRoot, groupPath, &ignoredError);
    }
}

bool writeShardPresets(const tui::RecorderTuiPreset& preset,
                       const std::filesystem::path& runRoot,
                       int shardCount,
                       int maxActivePerShard,
                       tui::RecorderTuiShardGrouping grouping,
                       std::vector<ShardProcess>& shards,
                       OutputGroupMap& groupDirs,
                       std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(runRoot / "presets", ec);
    std::filesystem::create_directories(runRoot / "status", ec);
    std::filesystem::create_directories(runRoot / "logs", ec);
    if (ec) {
        error = "failed to create shard run directory: " + runRoot.string();
        return false;
    }

    const std::filesystem::path groupMapPath = runRoot / "output_groups.tsv";
    const std::int64_t groupTimestampNs = wallNowNs();
    groupDirs.clear();
    for (const auto& job : preset.jobs) {
        const std::string normalizedSymbol = normalizedRecordingSymbolForJob(job);
        auto [it, inserted] = groupDirs.try_emplace(normalizedSymbol);
        if (inserted) {
            const std::string groupName = recordings::recordingGroupFolderName(groupTimestampNs, normalizedSymbol);
            it->second = uniquePath(preset.outputDir, groupName);
        }
    }
    if (!writeOutputGroupMap(groupMapPath, groupDirs, error)) return false;

    auto shardPresets = tui::splitPresetIntoShards(preset, shardCount, maxActivePerShard, grouping);
    shards.clear();
    shards.reserve(shardPresets.size());
    for (std::size_t i = 0; i < shardPresets.size(); ++i) {
        auto& shardPreset = shardPresets[i];
        std::ostringstream suffix;
        suffix << "shard";
        if (i + 1u < 10u) suffix << '0';
        suffix << (i + 1u);

        ShardProcess shard{};
        shard.index = static_cast<int>(i + 1u);
        shard.label = shardLabelForPreset(shardPreset, grouping);
        shard.presetPath = runRoot / "presets" / (suffix.str() + ".ini");
        shard.statusPath = runRoot / "status" / (suffix.str() + ".status");
        shard.logPath = runRoot / "logs" / (suffix.str() + ".log");
        shard.outputGroupMapPath = groupMapPath;
        shard.status.state = "queued";
        shard.status.message = "queued";
        shard.status.jobs = static_cast<int>(shardPreset.jobs.size());
        shard.status.pending = static_cast<int>(shardPreset.jobs.size());
        shard.exclusiveMarketDataSessionKeys = exclusiveMarketDataSessionKeysForPreset(shardPreset);
        if (!tui::savePresetFile(shard.presetPath, shardPreset, error)) return false;
        shards.push_back(std::move(shard));
    }
    return true;
}

pid_t spawnShard(const std::filesystem::path& exe, const ShardProcess& shard) {
    const pid_t pid = ::fork();
    if (pid != 0) return pid;

    const int logFd = ::open(shard.logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (logFd >= 0) {
        (void)::dup2(logFd, STDOUT_FILENO);
        (void)::dup2(logFd, STDERR_FILENO);
        (void)::close(logFd);
    }

    const std::string exeText = exe.string();
    const std::string presetText = shard.presetPath.string();
    const std::string statusText = shard.statusPath.string();
    const std::string outputGroupMapText = shard.outputGroupMapPath.string();
    if (shard.outputIsGroup) {
        ::execl(exeText.c_str(),
                exeText.c_str(),
                "run-preset",
                "--preset",
                presetText.c_str(),
                "--status",
                statusText.c_str(),
                "--output-group-map",
                outputGroupMapText.c_str(),
                "--output-is-group",
                static_cast<char*>(nullptr));
    } else {
        ::execl(exeText.c_str(),
                exeText.c_str(),
                "run-preset",
                "--preset",
                presetText.c_str(),
                "--status",
                statusText.c_str(),
                "--output-group-map",
                outputGroupMapText.c_str(),
                static_cast<char*>(nullptr));
    }
    std::fprintf(stderr, "exec failed: %s\n", std::strerror(errno));
    _exit(127);
}

void persistShardStatus(ShardProcess& shard) {
    shard.status.pid = static_cast<int>(shard.pid);
    shard.status.updatedWallNs = wallNowNs();
    if (tui::writeShardStatusFile(shard.statusPath, shard.status)) {
        shard.status.statusFilePresent = true;
        shard.status.statusTmpPresent = false;
        shard.status.statusTmpNonEmpty = false;
    }
}

void refreshShardStatus(ShardProcess& shard) {
    if (shard.exited) return;
    const auto diskStatus = tui::readShardStatusFile(shard.statusPath);
    if (diskStatus.statusFilePresent) {
        shard.status = diskStatus;
    } else {
        shard.status.statusFilePresent = false;
        shard.status.statusTmpPresent = diskStatus.statusTmpPresent;
        shard.status.statusTmpNonEmpty = diskStatus.statusTmpNonEmpty;
    }

    if (shard.pid <= 0 || shard.exited) return;
    std::ifstream status("/proc/" + std::to_string(shard.pid) + "/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            status >> shard.rssKb;
            return;
        }
        std::string rest;
        std::getline(status, rest);
    }
}

void markKillSentNotReaped(ShardProcess& shard) {
    if (shard.exited || !shard.launchStarted || shard.pid <= 0) return;
    shard.stopRequested = true;
    shard.exited = true;
    shard.status.state = "kill_sent_not_reaped";
    shard.status.message = "SIGKILL sent but waitpid did not reap";
    shard.status.running = 0;
    shard.status.starting = 0;
    shard.status.pending = 0;
    shard.status.errors = std::max(shard.status.errors, 1);
    shard.status.signal = SIGKILL;
    shard.status.exitCode = -1;
    shard.rssKb = 0;
    persistShardStatus(shard);
}

tui::RecorderTuiShardExit childExitFromWaitStatus(int status) noexcept {
    if (WIFEXITED(status)) return tui::RecorderTuiShardExit::exited(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return tui::RecorderTuiShardExit::signaled(WTERMSIG(status));
    return {};
}

void reapShard(ShardProcess& shard) {
    if (shard.pid <= 0 || shard.exited) return;
    int status = 0;
    const pid_t result = ::waitpid(shard.pid, &status, WNOHANG);
    if (result == shard.pid) {
        shard.exited = true;
        shard.exitStatus = status;
        shard.rssKb = 0;
        const auto diskStatus = tui::readShardStatusFile(shard.statusPath);
        if (diskStatus.statusFilePresent) shard.status = diskStatus;
        tui::applyShardChildExit(shard.status, childExitFromWaitStatus(status));
        persistShardStatus(shard);
    }
}

bool allExited(const std::vector<ShardProcess>& shards) {
    return std::all_of(shards.begin(), shards.end(), [](const ShardProcess& shard) { return shard.exited; });
}

void reapShardsUntil(std::vector<ShardProcess>& shards, Clock::time_point deadline) {
    while (!allExited(shards) && Clock::now() < deadline) {
        for (auto& shard : shards) {
            reapShard(shard);
            refreshShardStatus(shard);
        }
        if (!allExited(shards)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool cleanShardExit(const ShardProcess& shard) noexcept {
    if (!shard.exited || shard.status.errors != 0) return false;
    return shard.status.state == "done"
        || shard.status.state == "skipped"
        || shard.status.state == "stopped";
}

ShardRenderSummary summarizeShards(const std::vector<ShardProcess>& shards) noexcept {
    ShardRenderSummary summary{};
    for (const auto& shard : shards) {
        summary.rows += shard.status.rows;
        summary.rssKb += shard.rssKb;
        summary.running += shard.status.running;
        summary.starting += shard.status.starting;
        summary.stalled += shard.status.stalled;
        summary.pending += shard.status.pending;
        summary.skipped += shard.status.skipped;
        summary.errors += shard.status.errors;
        if (!shard.launchStarted && !shard.exited && !shard.stopRequested) {
            ++summary.queued;
        } else if (shard.launchStarted && !shard.exited) {
            ++summary.active;
        } else if (cleanShardExit(shard)) {
            ++summary.cleanExited;
            ++summary.completed;
        } else {
            ++summary.failedExited;
            ++summary.completed;
        }
    }
    return summary;
}

void stopShard(ShardProcess& shard) {
    if (shard.exited || shard.stopRequested) return;
    if (!shard.launchStarted) {
        shard.stopRequested = true;
        shard.exited = true;
        shard.status.state = "stopped";
        shard.status.message = "stop requested before launch";
        shard.status.running = 0;
        shard.status.starting = 0;
        shard.status.pending = 0;
        persistShardStatus(shard);
        return;
    }
    if (shard.pid <= 0) return;
    (void)::kill(shard.pid, SIGTERM);
    shard.stopRequested = true;
    shard.status.state = "stopping";
    shard.status.message = "stop requested";
    persistShardStatus(shard);
}

void stopAllShards(std::vector<ShardProcess>& shards) {
    for (auto& shard : shards) stopShard(shard);
}

int startQueuedShards(std::vector<ShardProcess>& shards,
                      const std::filesystem::path& exe,
                      int maxActiveShards,
                      std::uint64_t memoryLimitKb,
                      bool gradualAdmission) {
    int active = 0;
    std::uint64_t admittedRssKb = 0u;
    for (const auto& shard : shards) {
        if (shard.launchStarted && !shard.exited) {
            ++active;
            admittedRssKb += shard.rssKb != 0u ? shard.rssKb : 512u * 1024u;
        }
    }
    int slots = std::max(0, maxActiveShards - active);
    if (gradualAdmission) slots = std::min(slots, 1);
    int launched = 0;
    for (std::size_t index = 0; index < shards.size() && slots > 0; ++index) {
        auto& shard = shards[index];
        if (shard.launchStarted || shard.exited || shard.stopRequested) continue;
        if (exclusiveMarketDataSessionConflict(shard, shards)) continue;
        constexpr std::uint64_t kVenueAdmissionReserveKb = 512u * 1024u;
        if (memoryLimitKb != 0u && admittedRssKb + kVenueAdmissionReserveKb > memoryLimitKb) {
            shard.status.state = "resource_queued";
            shard.status.message = "waiting for recorder RSS budget";
            persistShardStatus(shard);
            break;
        }
        shard.launchStarted = true;
        shard.status.state = "spawned";
        shard.status.message = "spawn requested";
        shard.status.pending = 0;
        shard.status.starting = std::max(1, shard.status.starting);
        shard.pid = spawnShard(exe, shard);
        if (shard.pid < 0) {
            shard.status.state = "spawn_error";
            shard.status.message = std::strerror(errno);
            shard.status.errors = 1;
            shard.status.starting = 0;
            shard.exited = true;
        } else {
            shard.status.message = "spawned";
            ++launched;
            admittedRssKb += kVenueAdmissionReserveKb;
        }
        persistShardStatus(shard);
        --slots;
    }
    return launched;
}

void renderShards(const std::vector<ShardProcess>& shards,
                  const std::filesystem::path& runRoot,
                  int maxActiveShards,
                  std::string_view message) {
    const auto viewport = currentViewport();
    clearScreen();

    const ShardRenderSummary summary = summarizeShards(shards);

    std::ostringstream header;
    header << "hft-recorder TUI / sharded run"
           << " shards=" << shards.size()
           << " active=" << summary.active
           << " queued=" << summary.queued
           << " completed=" << summary.completed
           << " max_active=" << maxActiveShards
           << " running=" << summary.running
           << " starting=" << summary.starting
           << " stalled=" << summary.stalled
           << " pending=" << summary.pending
           << " skipped=" << summary.skipped
           << " clean_exit=" << summary.cleanExited
           << " failed=" << summary.failedExited
           << " errors=" << summary.errors
           << " rows=" << summary.rows
           << " rss=" << (summary.rssKb / 1024) << "MB";
    printLine(header.str(), viewport);
    printLine("run: " + tui::compactSessionPath(runRoot, std::max(20, viewport.cols - 5)), viewport);
    std::putchar('\n');

    std::vector<std::string> lines;
    std::vector<const ShardProcess*> rssTop;
    rssTop.reserve(shards.size());
    for (const auto& shard : shards) {
        if (shard.rssKb != 0u) rssTop.push_back(&shard);
    }
    std::sort(rssTop.begin(), rssTop.end(), [](const ShardProcess* lhs, const ShardProcess* rhs) {
        return lhs->rssKb > rhs->rssKb;
    });
    if (!rssTop.empty()) {
        std::ostringstream line;
        line << "top rss:";
        const std::size_t limit = std::min<std::size_t>(5u, rssTop.size());
        for (std::size_t i = 0; i < limit; ++i) {
            const auto& shard = *rssTop[i];
            line << " #" << shard.index;
            if (!shard.label.empty()) line << '(' << shard.label << ')';
            line << '=' << (shard.rssKb / 1024) << "MB";
        }
        lines.push_back(line.str());
    }

    std::vector<std::string> issueLines;
    std::vector<std::string> activeLines;
    std::vector<std::string> queuedLines;
    for (const auto& shard : shards) {
        const bool nonCleanExit = shard.exited && !cleanShardExit(shard);
        const bool issue = nonCleanExit || tui::shardStatusIsIssue(shard.status);
        const bool active = shard.launchStarted && !shard.exited;
        const bool queued = !shard.launchStarted && !shard.exited && !shard.stopRequested;

        if (issue) {
            std::ostringstream line;
            line << " #" << shard.index;
            if (!shard.label.empty()) line << " " << shard.label;
            line << " pid=" << shard.pid
                 << " " << shard.status.state
                 << " jobs=" << shard.status.jobs
                 << " run=" << shard.status.running
                 << " start=" << shard.status.starting
                 << " stall=" << shard.status.stalled
                 << " pend=" << shard.status.pending
                 << " done=" << shard.status.finalized
                 << " skip=" << shard.status.skipped
                 << " err=" << shard.status.errors
                 << " rows=" << shard.status.rows
                 << " rss=" << (shard.rssKb / 1024) << "MB";
            if (!shard.status.statusFilePresent) line << " missing_status=1";
            if (shard.status.statusTmpPresent) {
                line << " status_tmp=" << (shard.status.statusTmpNonEmpty ? "nonempty" : "empty");
            }
            if (!shard.status.message.empty()) line << " " << shard.status.message;
            const std::string& errorDetail = shard.status.lastError.empty()
                ? shard.status.firstError
                : shard.status.lastError;
            if (!errorDetail.empty() && errorDetail != shard.status.message) {
                line << " last_error=" << errorDetail;
            }
            issueLines.push_back(line.str());
        }

        if (active) {
            std::ostringstream line;
            line << " #" << shard.index;
            if (!shard.label.empty()) line << " " << shard.label;
            line << " pid=" << shard.pid
                 << " " << shard.status.state
                 << " jobs=" << shard.status.jobs
                 << " run=" << shard.status.running
                 << " start=" << shard.status.starting
                 << " stall=" << shard.status.stalled
                 << " pend=" << shard.status.pending
                 << " done=" << shard.status.finalized
                 << " rows=" << shard.status.rows
                 << " rss=" << (shard.rssKb / 1024) << "MB";
            if (!shard.status.message.empty()) line << " " << shard.status.message;
            const std::string& errorDetail = shard.status.lastError.empty()
                ? shard.status.firstError
                : shard.status.lastError;
            if (!errorDetail.empty() && errorDetail != shard.status.message) {
                line << " last_error=" << errorDetail;
            }
            activeLines.push_back(line.str());
        } else if (queued && queuedLines.size() < 8u) {
            std::ostringstream line;
            line << " #" << shard.index;
            if (!shard.label.empty()) line << " " << shard.label;
            line << " jobs=" << shard.status.jobs
                 << " pend=" << shard.status.pending
                 << " " << shard.status.state;
            queuedLines.push_back(line.str());
        }
    }

    if (issueLines.empty()) {
        lines.push_back("issues: none");
    } else {
        lines.push_back("issues:");
        lines.insert(lines.end(), issueLines.begin(), issueLines.end());
    }
    if (activeLines.empty()) {
        lines.push_back("active: none");
    } else {
        lines.push_back("active:");
        lines.insert(lines.end(), activeLines.begin(), activeLines.end());
    }
    if (summary.queued == 0) {
        lines.push_back("queued: none");
    } else {
        lines.push_back("queued next:");
        lines.insert(lines.end(), queuedLines.begin(), queuedLines.end());
        if (summary.queued > static_cast<int>(queuedLines.size())) {
            lines.push_back("... " + std::to_string(summary.queued - static_cast<int>(queuedLines.size())) + " more queued");
        }
    }

    const int reserved = message.empty() ? 6 : 7;
    for (const auto& line : tui::limitLinesForViewport(lines, viewport, reserved)) {
        printLine(line, viewport);
    }
    std::putchar('\n');
    printLine("[q] stop all and return", viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

char readControlKey(int timeoutMs) {
    pollfd fd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    const int rc = ::poll(&fd, 1, timeoutMs);
    if (rc <= 0 || (fd.revents & POLLIN) == 0) return 0;
    char ch = 0;
    if (::read(STDIN_FILENO, &ch, 1) != 1) return 0;
    return ch;
}

bool isStopKey(char ch) noexcept {
    return ch == 'q' || ch == 'Q';
}

int runShardSupervisor(const tui::RecorderTuiPreset& preset,
                       const std::filesystem::path& runRoot,
                       int shardCount,
                       int maxActivePerShard,
                       tui::RecorderTuiShardGrouping grouping,
                       int requestedMaxActiveShards) {
    ShardTerminalGuard terminalGuard;
    std::vector<ShardProcess> shards;
    OutputGroupMap groupDirs;
    std::string error;
    if (!writeShardPresets(preset, runRoot, shardCount, maxActivePerShard, grouping, shards, groupDirs, error)) {
        std::fprintf(stderr, "shard-run: %s\n", error.c_str());
        return 1;
    }

    const int maxActiveShards = configuredMaxActiveShards(
        preset,
        grouping,
        static_cast<int>(shards.size()),
        requestedMaxActiveShards);
    const std::filesystem::path exe = selfExecutablePath();
    for (auto& shard : shards) {
        persistShardStatus(shard);
    }
    const bool venueMultiplex = grouping == tui::RecorderTuiShardGrouping::ByVenue;
    const std::uint64_t memoryLimitKb = venueMultiplex
        ? static_cast<std::uint64_t>(std::max(512, preset.memoryLimitMiB)) * 1024u
        : 0u;
    const int initialLaunched = startQueuedShards(shards, exe, maxActiveShards, memoryLimitKb, venueMultiplex);

    std::string message = "queued " + std::to_string(shards.size())
        + " shard(s), started " + std::to_string(initialLaunched)
        + ", max_active_shards=" + std::to_string(maxActiveShards);
    auto nextRender = Clock::now();
    while (true) {
        for (auto& shard : shards) {
            reapShard(shard);
            refreshShardStatus(shard);
        }
        const int launched = startQueuedShards(shards, exe, maxActiveShards, memoryLimitKb, venueMultiplex);
        if (launched > 0 && message.empty()) {
            message = "started " + std::to_string(launched) + " queued shard(s)";
        }

        const auto now = Clock::now();
        if (now >= nextRender) {
            renderShards(shards, runRoot, maxActiveShards, message);
            message.clear();
            nextRender = now + std::chrono::seconds(1);
        }

        const char ch = readControlKey(250);
        if (isStopKey(ch)) {
            stopAllShards(shards);
            message = "stop requested for all shards";
            renderShards(shards, runRoot, maxActiveShards, message);
            break;
        }
        if (allExited(shards)) {
            writeSupervisorGroupManifests(preset.outputDir, groupDirs);
            message = "all shards exited; press q to return";
            renderShards(shards, runRoot, maxActiveShards, message);
            while (true) {
                const char doneKey = readControlKey(500);
                if (isStopKey(doneKey)) break;
            }
            return 0;
        }
    }

    const auto stopDeadline = Clock::now() + std::chrono::seconds(20);
    while (!allExited(shards) && Clock::now() < stopDeadline) {
        for (auto& shard : shards) {
            reapShard(shard);
            refreshShardStatus(shard);
        }
        renderShards(shards, runRoot, maxActiveShards, "waiting for shard finalization");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    for (auto& shard : shards) {
        if (!shard.exited && shard.launchStarted && shard.pid > 0) (void)::kill(shard.pid, SIGKILL);
    }
    reapShardsUntil(shards, Clock::now() + std::chrono::seconds(2));
    for (auto& shard : shards) markKillSentNotReaped(shard);
    if (allExited(shards)) writeSupervisorGroupManifests(preset.outputDir, groupDirs);
    renderShards(shards, runRoot, maxActiveShards, "stopped");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return 0;
}

void printShardRunUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder shard-run --preset path [--shards n] [--max-active-per-shard n] [--max-active-shards n] [--run-root path] [--isolate-jobs]");
}

}  // namespace

int runShardPresetInteractive(const tui::RecorderTuiPreset& preset, const std::filesystem::path&) {
    const auto grouping = preset.executionMode == tui::RecorderTuiExecutionMode::VenueMultiplex
        ? tui::RecorderTuiShardGrouping::ByVenue
        : tui::RecorderTuiShardGrouping::BySymbol;
    const int shardCount = defaultShardCount(preset, grouping);
    const int maxActivePerShard = tui::defaultRecorderTuiMaxActiveJobsPerShard(preset, grouping);
    return runShardSupervisor(preset, shardRunRoot(preset), shardCount, maxActivePerShard, grouping, 0);
}

int runShardRun(int argc, char** argv) {
    std::filesystem::path presetPath;
    std::filesystem::path runRoot;
    int shardCount = 0;
    int maxActivePerShard = 0;
    int maxActiveShards = 0;
    auto grouping = tui::RecorderTuiShardGrouping::BySymbol;
    bool groupingExplicit = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            printShardRunUsage();
            return 0;
        }
        if (arg == "--preset") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --preset requires a path\n", stderr);
                return 2;
            }
            presetPath = tui::resolvePresetPath(argv[++i]);
            continue;
        }
        if (arg == "--shards") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --shards requires a value\n", stderr);
                return 2;
            }
            shardCount = std::max(1, std::atoi(argv[++i]));
            continue;
        }
        if (arg == "--max-active-per-shard") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --max-active-per-shard requires a value\n", stderr);
                return 2;
            }
            maxActivePerShard = std::max(1, std::atoi(argv[++i]));
            continue;
        }
        if (arg == "--max-active-shards") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --max-active-shards requires a value\n", stderr);
                return 2;
            }
            maxActiveShards = std::max(1, std::atoi(argv[++i]));
            continue;
        }
        if (arg == "--run-root") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --run-root requires a path\n", stderr);
                return 2;
            }
            runRoot = argv[++i];
            continue;
        }
        if (arg == "--isolate-jobs") {
            grouping = tui::RecorderTuiShardGrouping::ByJob;
            groupingExplicit = true;
            continue;
        }
        std::fprintf(stderr, "shard-run: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        printShardRunUsage();
        return 2;
    }
    if (presetPath.empty()) {
        std::fputs("shard-run: --preset is required\n", stderr);
        return 2;
    }

    tui::RecorderTuiPreset preset{};
    std::string error;
    if (!tui::loadPresetFile(presetPath, preset, error)) {
        std::fprintf(stderr, "shard-run: %s\n", error.c_str());
        return 1;
    }
    if (preset.jobs.empty()) {
        std::fputs("shard-run: preset has no jobs\n", stderr);
        return 2;
    }
    if (!groupingExplicit && preset.executionMode == tui::RecorderTuiExecutionMode::VenueMultiplex) {
        grouping = tui::RecorderTuiShardGrouping::ByVenue;
    }
    if (shardCount <= 0) shardCount = defaultShardCount(preset, grouping);
    if (grouping == tui::RecorderTuiShardGrouping::ByJob) {
        shardCount = std::max(shardCount, static_cast<int>(preset.jobs.size()));
    }
    if (maxActivePerShard <= 0) {
        maxActivePerShard = tui::defaultRecorderTuiMaxActiveJobsPerShard(preset, grouping);
    }
    if (runRoot.empty()) runRoot = shardRunRoot(preset);
    return runShardSupervisor(preset, runRoot, shardCount, maxActivePerShard, grouping, maxActiveShards);
}

}  // namespace hftrec::app
