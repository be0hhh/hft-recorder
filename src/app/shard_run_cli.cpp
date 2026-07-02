#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
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

#include "core/tui/RecorderTuiPreset.hpp"
#include "core/tui/RecorderTuiShard.hpp"
#include "core/tui/TerminalRender.hpp"

namespace hftrec::app {

namespace {

using Clock = std::chrono::steady_clock;

struct ShardStatus {
    std::string state{"starting"};
    std::string message{};
    int jobs{0};
    int running{0};
    int starting{0};
    int stalled{0};
    int pending{0};
    int finalized{0};
    int errors{0};
    std::uint64_t rows{0};
};

struct ShardProcess {
    int index{0};
    std::filesystem::path presetPath;
    std::filesystem::path statusPath;
    std::filesystem::path logPath;
    pid_t pid{-1};
    bool stopRequested{false};
    bool exited{false};
    int exitStatus{0};
    std::uint64_t rssKb{0};
    ShardStatus status{};
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

int defaultShardCount(const tui::RecorderTuiPreset& preset) {
    if (preset.jobs.empty()) return 1;
    const int perShard = std::max(1, preset.maxActiveJobs);
    const int needed = (static_cast<int>(preset.jobs.size()) + perShard - 1) / perShard;
    return std::max(1, std::min(8, needed));
}

std::filesystem::path shardRunRoot(const tui::RecorderTuiPreset& preset) {
    std::ostringstream name;
    name << wallNowNs();
    return preset.outputDir / ".shards" / name.str();
}

bool writeShardPresets(const tui::RecorderTuiPreset& preset,
                       const std::filesystem::path& runRoot,
                       int shardCount,
                       int maxActivePerShard,
                       std::vector<ShardProcess>& shards,
                       std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(runRoot / "presets", ec);
    std::filesystem::create_directories(runRoot / "status", ec);
    std::filesystem::create_directories(runRoot / "logs", ec);
    if (ec) {
        error = "failed to create shard run directory: " + runRoot.string();
        return false;
    }

    const auto shardPresets = tui::splitPresetIntoShards(preset, shardCount, maxActivePerShard);
    shards.clear();
    shards.reserve(shardPresets.size());
    for (std::size_t i = 0; i < shardPresets.size(); ++i) {
        std::ostringstream suffix;
        suffix << "shard";
        if (i + 1u < 10u) suffix << '0';
        suffix << (i + 1u);

        ShardProcess shard{};
        shard.index = static_cast<int>(i + 1u);
        shard.presetPath = runRoot / "presets" / (suffix.str() + ".ini");
        shard.statusPath = runRoot / "status" / (suffix.str() + ".status");
        shard.logPath = runRoot / "logs" / (suffix.str() + ".log");
        if (!tui::savePresetFile(shard.presetPath, shardPresets[i], error)) return false;
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
    ::execl(exeText.c_str(),
            exeText.c_str(),
            "run-preset",
            "--preset",
            presetText.c_str(),
            "--status",
            statusText.c_str(),
            static_cast<char*>(nullptr));
    std::fprintf(stderr, "exec failed: %s\n", std::strerror(errno));
    _exit(127);
}

std::map<std::string, std::string> readKeyValueFile(const std::filesystem::path& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out.emplace(line.substr(0, eq), line.substr(eq + 1u));
    }
    return out;
}

int intValue(const std::map<std::string, std::string>& values, std::string_view key) {
    const auto it = values.find(std::string{key});
    if (it == values.end()) return 0;
    return std::atoi(it->second.c_str());
}

std::uint64_t u64Value(const std::map<std::string, std::string>& values, std::string_view key) {
    const auto it = values.find(std::string{key});
    if (it == values.end()) return 0;
    return static_cast<std::uint64_t>(std::strtoull(it->second.c_str(), nullptr, 10));
}

void refreshShardStatus(ShardProcess& shard) {
    const bool preserveExitStatus = shard.exited;
    const std::string exitState = shard.status.state;
    const std::string exitMessage = shard.status.message;
    const int exitErrors = shard.status.errors;

    const auto values = readKeyValueFile(shard.statusPath);
    if (!values.empty()) {
        if (const auto it = values.find("state"); it != values.end()) shard.status.state = it->second;
        if (const auto it = values.find("message"); it != values.end()) shard.status.message = it->second;
        shard.status.jobs = intValue(values, "jobs");
        shard.status.running = intValue(values, "running");
        shard.status.starting = intValue(values, "starting");
        shard.status.stalled = intValue(values, "stalled");
        shard.status.pending = intValue(values, "pending");
        shard.status.finalized = intValue(values, "finalized");
        shard.status.errors = intValue(values, "errors");
        shard.status.rows = u64Value(values, "rows");
    }

    if (preserveExitStatus) {
        shard.status.state = exitState;
        shard.status.message = exitMessage;
        shard.status.errors = std::max(shard.status.errors, exitErrors);
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

void reapShard(ShardProcess& shard) {
    if (shard.pid <= 0 || shard.exited) return;
    int status = 0;
    const pid_t result = ::waitpid(shard.pid, &status, WNOHANG);
    if (result == shard.pid) {
        shard.exited = true;
        shard.exitStatus = status;
        shard.rssKb = 0;
        const bool cleanExit = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (WIFEXITED(status)) {
            shard.status.message = "exit=" + std::to_string(WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            shard.status.message = "signal=" + std::to_string(WTERMSIG(status));
        } else {
            shard.status.message = "exit=unknown";
        }
        if (!cleanExit && shard.status.errors == 0) shard.status.errors = 1;
        if (shard.status.state == "starting" || shard.status.state == "running") {
            shard.status.state = cleanExit ? "done" : "exited";
        }
    }
}

bool allExited(const std::vector<ShardProcess>& shards) {
    return std::all_of(shards.begin(), shards.end(), [](const ShardProcess& shard) { return shard.exited; });
}

void stopShard(ShardProcess& shard) {
    if (shard.pid <= 0 || shard.exited || shard.stopRequested) return;
    (void)::kill(shard.pid, SIGTERM);
    shard.stopRequested = true;
}

void stopAllShards(std::vector<ShardProcess>& shards) {
    for (auto& shard : shards) stopShard(shard);
}

void renderShards(const std::vector<ShardProcess>& shards,
                  const std::filesystem::path& runRoot,
                  std::string_view message) {
    const auto viewport = currentViewport();
    clearScreen();

    std::uint64_t rows = 0;
    std::uint64_t rssKb = 0;
    int running = 0;
    int starting = 0;
    int stalled = 0;
    int errors = 0;
    int alive = 0;
    for (const auto& shard : shards) {
        rows += shard.status.rows;
        rssKb += shard.rssKb;
        running += shard.status.running;
        starting += shard.status.starting;
        stalled += shard.status.stalled;
        errors += shard.status.errors;
        if (!shard.exited) ++alive;
    }

    std::ostringstream header;
    header << "hft-recorder TUI / sharded run"
           << " shards=" << shards.size()
           << " alive=" << alive
           << " running=" << running
           << " starting=" << starting
           << " stalled=" << stalled
           << " errors=" << errors
           << " rows=" << rows
           << " rss=" << (rssKb / 1024) << "MB";
    printLine(header.str(), viewport);
    printLine("run: " + tui::compactSessionPath(runRoot, std::max(20, viewport.cols - 5)), viewport);
    std::putchar('\n');

    std::vector<std::string> lines;
    for (const auto& shard : shards) {
        std::ostringstream line;
        line << " " << shard.index
             << " pid=" << shard.pid
             << " " << (shard.exited ? "exited" : shard.status.state)
             << " jobs=" << shard.status.jobs
             << " run=" << shard.status.running
             << " start=" << shard.status.starting
             << " stall=" << shard.status.stalled
             << " pend=" << shard.status.pending
             << " done=" << shard.status.finalized
             << " err=" << shard.status.errors
             << " rows=" << shard.status.rows
             << " rss=" << (shard.rssKb / 1024) << "MB";
        if (!shard.status.message.empty()) line << " " << shard.status.message;
        line
             << " log=" << tui::compactSessionPath(shard.logPath, std::max(16, viewport.cols / 3));
        lines.push_back(line.str());
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
                       int maxActivePerShard) {
    std::vector<ShardProcess> shards;
    std::string error;
    if (!writeShardPresets(preset, runRoot, shardCount, maxActivePerShard, shards, error)) {
        std::fprintf(stderr, "shard-run: %s\n", error.c_str());
        return 1;
    }

    const std::filesystem::path exe = selfExecutablePath();
    for (auto& shard : shards) {
        shard.pid = spawnShard(exe, shard);
        if (shard.pid < 0) {
            shard.status.state = "spawn_error";
            shard.status.message = std::strerror(errno);
            shard.exited = true;
        }
    }

    std::string message = "started " + std::to_string(shards.size()) + " shard(s)";
    auto nextRender = Clock::now();
    while (true) {
        for (auto& shard : shards) {
            reapShard(shard);
            refreshShardStatus(shard);
        }

        const auto now = Clock::now();
        if (now >= nextRender) {
            renderShards(shards, runRoot, message);
            message.clear();
            nextRender = now + std::chrono::seconds(1);
        }

        const char ch = readControlKey(250);
        if (isStopKey(ch)) {
            stopAllShards(shards);
            message = "stop requested for all shards";
            renderShards(shards, runRoot, message);
            break;
        }
        if (allExited(shards)) {
            message = "all shards exited; press q to return";
            renderShards(shards, runRoot, message);
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
        renderShards(shards, runRoot, "waiting for shard finalization");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    for (auto& shard : shards) {
        if (!shard.exited && shard.pid > 0) (void)::kill(shard.pid, SIGKILL);
    }
    for (auto& shard : shards) reapShard(shard);
    renderShards(shards, runRoot, "stopped");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return 0;
}

void printShardRunUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder shard-run --preset path [--shards n] [--max-active-per-shard n] [--run-root path]");
}

}  // namespace

int runShardPresetInteractive(const tui::RecorderTuiPreset& preset, const std::filesystem::path&) {
    const int shardCount = defaultShardCount(preset);
    const int maxActivePerShard = std::max(1, preset.maxActiveJobs);
    return runShardSupervisor(preset, shardRunRoot(preset), shardCount, maxActivePerShard);
}

int runShardRun(int argc, char** argv) {
    std::filesystem::path presetPath;
    std::filesystem::path runRoot;
    int shardCount = 0;
    int maxActivePerShard = 0;

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
        if (arg == "--run-root") {
            if (i + 1 >= argc) {
                std::fputs("shard-run: --run-root requires a path\n", stderr);
                return 2;
            }
            runRoot = argv[++i];
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
    if (shardCount <= 0) shardCount = defaultShardCount(preset);
    if (maxActivePerShard <= 0) maxActivePerShard = std::max(1, preset.maxActiveJobs);
    if (runRoot.empty()) runRoot = shardRunRoot(preset);
    return runShardSupervisor(preset, runRoot, shardCount, maxActivePerShard);
}

}  // namespace hftrec::app
