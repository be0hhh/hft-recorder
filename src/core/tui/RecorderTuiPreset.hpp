#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hftrec::tui {

struct ChannelSelection {
    bool trades{false};
    bool liquidations{false};
    bool bookTicker{false};
    bool orderbook{false};
    bool markPrice{false};
    bool indexPrice{false};
    bool funding{false};
    bool priceLimit{false};
};

struct RecorderTuiJob {
    std::string name{"job"};
    std::string exchange{"binance"};
    std::string market{"futures"};
    std::string symbol{"BTCUSDT"};
    std::int64_t durationMin{0};
    ChannelSelection channels{};
};

struct RecorderTuiPreset {
    std::filesystem::path outputDir{"./recordings"};
    int progressSec{10};
    int launchWaveSize{4};
    int launchStaggerMs{250};
    int sameExchangeCooldownMs{1500};
    int maxActiveJobs{24};
    std::vector<RecorderTuiJob> jobs{};
};

ChannelSelection allLiveChannels() noexcept;
bool anyChannelSelected(const ChannelSelection& channels) noexcept;

bool parseDurationMinutes(std::string_view text, std::int64_t& out, std::string& error);
bool parseChannelSelection(std::string_view text, ChannelSelection& out, std::string& error);
std::string renderChannelSelection(const ChannelSelection& channels);

bool parsePresetText(std::string_view text, RecorderTuiPreset& out, std::string& error);
std::string renderPresetText(const RecorderTuiPreset& preset);

bool loadPresetFile(const std::filesystem::path& path, RecorderTuiPreset& out, std::string& error);
bool savePresetFile(const std::filesystem::path& path, const RecorderTuiPreset& preset, std::string& error);
std::filesystem::path presetConfigDir();
std::filesystem::path resolvePresetPath(std::string_view text);
std::filesystem::path defaultPresetPath();

}  // namespace hftrec::tui
