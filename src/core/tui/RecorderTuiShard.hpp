#pragma once

#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::tui {

std::vector<RecorderTuiPreset> splitPresetIntoShards(const RecorderTuiPreset& preset,
                                                     int shardCount,
                                                     int maxActiveJobsPerShard);

}  // namespace hftrec::tui
