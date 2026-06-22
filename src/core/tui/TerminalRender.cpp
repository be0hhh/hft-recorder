#include "core/tui/TerminalRender.hpp"

#include <algorithm>

namespace hftrec::tui {

TerminalViewport sanitizeViewport(TerminalViewport viewport) noexcept {
    if (viewport.rows < 8) viewport.rows = 8;
    if (viewport.cols < 24) viewport.cols = 24;
    return viewport;
}

std::string truncateForTerminal(std::string_view text, int cols) {
    if (cols <= 0) return {};
    const auto width = static_cast<std::size_t>(cols);
    if (text.size() <= width) return std::string{text};
    if (width <= 3u) return std::string(width, '.');

    std::string out{text.substr(0, width - 3u)};
    out.append("...");
    return out;
}

std::string compactSessionPath(const std::filesystem::path& path, int cols) {
    if (cols <= 0) return {};
    const std::string full = path.generic_string();
    if (full.size() <= static_cast<std::size_t>(cols)) return full;

    const std::string leaf = path.filename().generic_string();
    if (leaf.empty()) return truncateForTerminal(full, cols);
    const std::string compact = "..." + leaf;
    if (compact.size() <= static_cast<std::size_t>(cols)) return compact;
    return truncateForTerminal(leaf, cols);
}

std::vector<std::string> limitLinesForViewport(const std::vector<std::string>& lines,
                                               TerminalViewport viewport,
                                               int reservedLines) {
    viewport = sanitizeViewport(viewport);
    const int available = std::max(0, viewport.rows - std::max(0, reservedLines));
    if (available == 0 || lines.empty()) return {};

    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(std::min<int>(available, static_cast<int>(lines.size()))));
    const auto pushTruncated = [&](std::string_view line) {
        out.push_back(truncateForTerminal(line, viewport.cols));
    };

    if (static_cast<int>(lines.size()) <= available) {
        for (const auto& line : lines) pushTruncated(line);
        return out;
    }

    if (available == 1) {
        pushTruncated("... " + std::to_string(lines.size()) + " more lines hidden");
        return out;
    }

    const int visibleLines = available - 1;
    for (int i = 0; i < visibleLines; ++i) pushTruncated(lines[static_cast<std::size_t>(i)]);
    pushTruncated("... " + std::to_string(lines.size() - static_cast<std::size_t>(visibleLines)) + " more lines hidden");
    return out;
}

bool highRateChannelSelected(const ChannelSelection& channels) noexcept {
    return channels.bookTicker || channels.orderbook;
}

RunLoadClass classifyRunLoad(const RecorderTuiPreset& preset) noexcept {
    int indefiniteHighRateJobs = 0;
    for (const auto& job : preset.jobs) {
        if (job.durationMin == 0 && highRateChannelSelected(job.channels)) ++indefiniteHighRateJobs;
    }
    return indefiniteHighRateJobs > 1 ? RunLoadClass::Heavy : RunLoadClass::Normal;
}

bool requiresHeavyRunConfirmation(const RecorderTuiPreset& preset) noexcept {
    return classifyRunLoad(preset) == RunLoadClass::Heavy;
}

std::string heavyRunWarning(const RecorderTuiPreset& preset) {
    int indefiniteHighRateJobs = 0;
    for (const auto& job : preset.jobs) {
        if (job.durationMin == 0 && highRateChannelSelected(job.channels)) ++indefiniteHighRateJobs;
    }
    return "heavy run: " + std::to_string(indefiniteHighRateJobs) +
           " indefinite high-rate jobs. Press uppercase R to start.";
}

}  // namespace hftrec::tui
