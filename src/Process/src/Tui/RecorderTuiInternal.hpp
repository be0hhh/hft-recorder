#pragma once

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

#include "../../../Runtime/src/Capture/Coordinator/CaptureChannelSupport.hpp"
#include "../../../Runtime/src/Capture/Coordinator/CaptureCoordinator.hpp"
#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "RecorderTuiLaunch.hpp"
#include "RecorderTuiPreset.hpp"
#include "RecorderTuiSymbols.hpp"
#include "TerminalRender.hpp"


namespace hftrec::app {
int runShardPresetInteractive(const tui::RecorderTuiPreset& preset, const std::filesystem::path& presetPath);
}

namespace hftrec::app::tui_detail {

using Clock = std::chrono::steady_clock;
extern volatile std::sig_atomic_t gInterrupted;

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


struct GeneratedJobsAppendResult {
    std::size_t symbols{0};
    std::size_t added{0};
    std::size_t skipped{0};
    std::vector<std::filesystem::path> loadedFiles{};
    std::string error{};
};


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


struct RunOutputGroups {
    std::filesystem::path root{};
    std::int64_t timestampNs{0};
    std::map<std::string, std::filesystem::path> bySymbol{};
};


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
    std::uint64_t finalRows{0u};
    std::string status{"idle"};
    std::string planNote{};
    std::string error{};
};

constexpr std::int64_t kStartSlotGraceSec = 15;
constexpr auto kDeadZeroRowSessionTtl = std::chrono::minutes(5);
constexpr auto kDeadZeroRowSweepInterval = std::chrono::seconds(30);


struct DeadSessionSweepResult {
    int removedSessions{0};
    int removedGroups{0};
};


void handleSignal(int);

std::string trim(std::string_view text);

std::string lower(std::string_view text);

std::int64_t wallNowNs();

std::filesystem::path uniquePath(const std::filesystem::path& parent, const std::string& baseName);

Key readKey(int timeoutMs);

void clearScreen();

tui::TerminalViewport currentViewport() noexcept;

void printLine(std::string_view line, tui::TerminalViewport viewport);

std::string promptLine(TerminalGuard& terminal, std::string_view label, std::string_view current = {});

std::string jobLabel(const tui::RecorderTuiJob& job);

bool sameCaptureJob(const tui::RecorderTuiJob& lhs, const tui::RecorderTuiJob& rhs);

bool containsCaptureJob(const std::vector<tui::RecorderTuiJob>& jobs, const tui::RecorderTuiJob& candidate);

void addDefaultJob(tui::RecorderTuiPreset& preset);

void duplicateJob(tui::RecorderTuiPreset& preset, std::size_t index);

GeneratedJobsAppendResult appendGeneratedSymbolJobs(tui::RecorderTuiPreset& preset, std::string_view input);

std::string generatedJobsMessage(const GeneratedJobsAppendResult& result, std::string_view action);

void toggleChannelByIndex(tui::ChannelSelection& channels,int index);

bool channelByIndex(const tui::ChannelSelection& channels,int index);

const char* channelNameByIndex(int index);

capture::CaptureChannel captureChannelForLaunch(tui::LaunchChannel channel) noexcept;

tui::LaunchChannel launchChannelForCapture(capture::CaptureChannel channel) noexcept;

std::vector<capture::CaptureChannel> selectedCaptureChannels(const tui::ChannelSelection& channels);

bool tuiChannelAvailableUncached(const tui::RecorderTuiJob& job, tui::LaunchChannel channel);

bool tuiChannelAvailable(const tui::RecorderTuiJob& job, tui::LaunchChannel channel, void* userData);

void renderEditJob(const tui::RecorderTuiJob& job, int row, std::string_view message);

void editJob(TerminalGuard& terminal, tui::RecorderTuiJob& job);

void renderMainMenu(const tui::RecorderTuiPreset& preset, std::size_t selected, const std::filesystem::path& presetPath, std::string_view message);

RunOutputGroups makeRunOutputGroups(const tui::RecorderTuiPreset& preset);

std::filesystem::path outputDirForRunJob(RunOutputGroups& groups, const tui::RecorderTuiJob& job);

capture::CaptureConfig makeCaptureConfig(const tui::RecorderTuiJob& job,
                                         const std::filesystem::path& outputDir);

std::int64_t startAgeSeconds(const RunningJob& job, Clock::time_point now) noexcept;

bool startStalled(const RunningJob& job, Clock::time_point now) noexcept;

bool manifestHasRows(const capture::SessionManifest& manifest) noexcept;

std::string readTextFile(const std::filesystem::path& path);

std::string skippedChannelsNote(const tui::ChannelSelection& channels);

void mergeChannelSelection(tui::ChannelSelection& target, const tui::ChannelSelection& source) noexcept;

void appendPlanNote(std::string& target, const std::string& note);

std::string launchPlanMessage(const tui::RecorderTuiLaunchPlan& plan);

RunningJob makePlannedJob(const tui::RecorderTuiLaunchJob& planned,
                          Clock::time_point baseTime,
                          RunOutputGroups& outputGroups);

void appendPlannedJobs(std::vector<RunningJob>& jobs,
                       const tui::RecorderTuiLaunchPlan& plan,
                       Clock::time_point baseTime,
                       RunOutputGroups& outputGroups);

void appendStartError(RunningJob& job, std::string_view channel, Status status);

void tryStartChannel(RunningJob& job, std::string_view name, Status status);

void appendJobError(RunningJob& job, std::string message);

void applyPreflightPlan(RunningJob& job, const capture::CaptureLaunchPlan& plan);

bool preflightJobBeforeSession(RunningJob& job);

bool anyRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept;

int activeJobSlots(const std::vector<RunningJob>& jobs) noexcept;

bool exclusiveMarketDataSessionBlocked(const RunningJob& candidate, const std::vector<RunningJob>& jobs);

void requestStopJob(RunningJob& job);

RunningJob startJobFromConfig(const tui::RecorderTuiJob& source, capture::CaptureConfig config);

bool preparePlannedJobChannels(RunningJob& job, TuiChannelAvailabilityCache& availabilityCache);

std::shared_ptr<RunningJob> prepareAndStartJobWorker(tui::RecorderTuiJob source,
                                                     capture::CaptureConfig config,
                                                     Clock::time_point scheduledStart);

void startPlannedJobAsync(RunningJob& job);

bool completeStartJobIfReady(RunningJob& job);

void waitForStartJob(RunningJob& job);

bool looksLikeSessionDirName(std::string_view name) noexcept;

bool sessionDirHasJsonlRows(const std::filesystem::path& sessionDir);

bool deadZeroRowManifest(const capture::SessionManifest& manifest, std::int64_t nowNs) noexcept;

bool removeOrphanSessionDirIfDead(const std::filesystem::path& sessionDir,
                                  std::filesystem::file_time_type now,
                                  DeadSessionSweepResult& result);

bool removeSessionDirIfDeadZeroRows(const std::filesystem::path& sessionDir,
                                    std::int64_t nowNs,
                                    DeadSessionSweepResult& result);

void removeGroupIfEmpty(const std::filesystem::path& root,
                        const std::filesystem::path& groupPath,
                        DeadSessionSweepResult& result);

DeadSessionSweepResult sweepDeadZeroRowSessionsInGroup(const std::filesystem::path& root,
                                                       const std::filesystem::path& groupPath,
                                                       std::int64_t nowNs);

DeadSessionSweepResult sweepDeadZeroRowSessions(const std::filesystem::path& root, std::int64_t nowNs);

DeadSessionSweepResult sweepRunOutputGroups(const RunOutputGroups& outputGroups);

std::string deadSessionSweepMessage(const DeadSessionSweepResult& result);

void writeRunGroupManifests(const std::filesystem::path& recordingsRoot, const RunOutputGroups& outputGroups);

void finalizeJob(RunningJob& job);

std::uint64_t totalRows(const capture::CaptureCoordinator& coordinator) noexcept;

bool deadZeroRowJobExpired(const RunningJob& job, Clock::time_point now) noexcept;

bool cullDeadZeroRowJob(RunningJob& job, Clock::time_point now);

bool allJobsFinalized(const std::vector<RunningJob>& jobs) noexcept;

bool jobStatusIsIssue(std::string_view status) noexcept;

void renderRunning(const std::vector<RunningJob>& jobs, std::size_t selected, std::string_view message);

void runJobs(TerminalGuard& terminal, const tui::RecorderTuiPreset& preset);

void printUsage();

}  // namespace hftrec::app::tui_detail
