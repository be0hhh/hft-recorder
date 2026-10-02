#include "RecorderTuiInternal.hpp"

namespace hftrec::app::tui_detail {

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
    for (int i = 0; i < 3; ++i) {
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
    printLine("[a] add  [g] gen symbols  [Enter] edit  [c] copy  [d] delete  [w] save  [s] save as  [l] load  [r] start  [R] direct  [q] quit",
              viewport);
    if (!message.empty()) printLine(message, viewport);
    std::fflush(stdout);
}

bool jobStatusIsIssue(std::string_view status) noexcept {
    return status == "error"
        || status == "done_warn"
        || status == "dead"
        || status == "failed_empty"
        || status == "preflight_failed"
        || status == "stopped_starting";
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
        aggregateRows += job.finalized ? job.finalRows : (job.coordinator ? totalRows(*job.coordinator) : 0u);
        if (job.running) ++runningCount;
        if (job.startInProgress) ++startingCount;
        if (startStalled(job, now)) ++stalledCount;
        if (!job.launched && !job.finalized && !job.startInProgress) ++pendingCount;
        if (job.status == "skipped") ++skippedCount;
        if (jobStatusIsIssue(job.status)) ++errorCount;
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
                   << " bbo=" << static_cast<unsigned long long>(job.coordinator->bookTickerCount())
                   << " depth=" << static_cast<unsigned long long>(job.coordinator->depthCount());
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

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder tui [--preset path] [--output-dir path] [--progress-sec n]");
}

}  // namespace hftrec::app::tui_detail
