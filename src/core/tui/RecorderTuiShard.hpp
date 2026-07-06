#pragma once

#include <cstddef>
#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::tui {

enum class RecorderTuiShardGrouping {
    BySymbol,
    ByJob,
};

std::vector<RecorderTuiPreset> splitPresetIntoShards(const RecorderTuiPreset& preset,
                                                     int shardCount,
                                                     int maxActiveJobsPerShard,
                                                     RecorderTuiShardGrouping grouping = RecorderTuiShardGrouping::BySymbol);

struct RecorderTuiShardLaunchState {
    bool launchStarted{false};
    bool exited{false};
    bool stopRequested{false};
};

struct RecorderTuiShardLaunchDecision {
    std::vector<std::size_t> indices{};
    int active{0};
    int queued{0};
};

int clampRecorderTuiMaxActiveShards(int requested, int shardCount) noexcept;
int defaultRecorderTuiMaxActiveJobsPerShard(const RecorderTuiPreset& preset,
                                            RecorderTuiShardGrouping grouping) noexcept;
int defaultRecorderTuiMaxActiveShards(const RecorderTuiPreset& preset,
                                      RecorderTuiShardGrouping grouping,
                                      int shardCount) noexcept;
RecorderTuiShardLaunchDecision chooseQueuedShardLaunches(const std::vector<RecorderTuiShardLaunchState>& states,
                                                         int maxActiveShards);
void markQueuedShardLaunchesStopped(std::vector<RecorderTuiShardLaunchState>& states) noexcept;

}  // namespace hftrec::tui
