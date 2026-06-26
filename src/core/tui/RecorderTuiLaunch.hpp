#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::tui {

enum class LaunchChannel : std::uint8_t {
    Trades,
    Liquidations,
    BookTicker,
    Orderbook,
    MarkPrice,
    IndexPrice,
    Funding,
    PriceLimit,
};

struct RecorderTuiLaunchJob {
    RecorderTuiJob job{};
    ChannelSelection skippedChannels{};
    std::size_t originalIndex{0};
    std::int64_t scheduledStartMs{0};
    bool skipJob{false};
    std::string skipReason{};
};

struct RecorderTuiLaunchPlan {
    int launchWaveSize{4};
    int launchStaggerMs{250};
    int sameExchangeCooldownMs{1500};
    int maxActiveJobs{24};
    std::vector<RecorderTuiLaunchJob> jobs{};
    std::size_t runnableJobs{0};
    std::size_t skippedJobs{0};
};

using RecorderTuiChannelAvailabilityFn = bool (*)(const RecorderTuiJob& job, LaunchChannel channel, void* userData);

const char* launchChannelName(LaunchChannel channel) noexcept;
bool launchChannelSelected(const ChannelSelection& channels, LaunchChannel channel) noexcept;
void setLaunchChannel(ChannelSelection& channels, LaunchChannel channel, bool enabled) noexcept;
bool anyLaunchChannelSelected(const ChannelSelection& channels) noexcept;

RecorderTuiLaunchJob filterLaunchJobChannels(const RecorderTuiLaunchJob& planned,
                                             RecorderTuiChannelAvailabilityFn availability,
                                             void* userData);

RecorderTuiLaunchPlan buildLaunchPlan(const RecorderTuiPreset& preset,
                                      RecorderTuiChannelAvailabilityFn availability,
                                      void* userData);

}  // namespace hftrec::tui
