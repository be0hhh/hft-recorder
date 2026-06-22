#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::tui {

struct TerminalViewport {
    int rows{24};
    int cols{80};
};

enum class RunLoadClass {
    Normal,
    Heavy,
};

TerminalViewport sanitizeViewport(TerminalViewport viewport) noexcept;
std::string truncateForTerminal(std::string_view text, int cols);
std::string compactSessionPath(const std::filesystem::path& path, int cols);
std::vector<std::string> limitLinesForViewport(const std::vector<std::string>& lines,
                                               TerminalViewport viewport,
                                               int reservedLines);

bool highRateChannelSelected(const ChannelSelection& channels) noexcept;
RunLoadClass classifyRunLoad(const RecorderTuiPreset& preset) noexcept;
bool requiresHeavyRunConfirmation(const RecorderTuiPreset& preset) noexcept;
std::string heavyRunWarning(const RecorderTuiPreset& preset);

}  // namespace hftrec::tui
