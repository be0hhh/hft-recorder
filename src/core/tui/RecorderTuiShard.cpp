#include "core/tui/RecorderTuiShard.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "core/recordings/RecordingDiscovery.hpp"

namespace hftrec::tui {

std::vector<RecorderTuiPreset> splitPresetIntoShards(const RecorderTuiPreset& preset,
                                                     int shardCount,
                                                     int maxActiveJobsPerShard) {
    std::vector<std::pair<std::string, std::vector<RecorderTuiJob>>> groups;
    for (const auto& job : preset.jobs) {
        std::string symbol = recordings::normalizeRecordingSymbol(job.symbol);
        if (symbol.empty()) symbol = job.symbol;
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& item) { return item.first == symbol; });
        if (it == groups.end()) {
            groups.push_back({std::move(symbol), {job}});
        } else {
            it->second.push_back(job);
        }
    }

    const int groupCount = static_cast<int>(groups.empty() ? 1u : groups.size());
    const int count = std::max(1, std::min(shardCount, groupCount));
    std::vector<RecorderTuiPreset> shards;
    shards.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        RecorderTuiPreset shard = preset;
        shard.jobs.clear();
        shard.maxActiveJobs = std::max(1, maxActiveJobsPerShard);
        shards.push_back(std::move(shard));
    }

    for (std::size_t i = 0; i < groups.size(); ++i) {
        auto& shardJobs = shards[i % shards.size()].jobs;
        shardJobs.insert(shardJobs.end(), groups[i].second.begin(), groups[i].second.end());
    }
    return shards;
}

}  // namespace hftrec::tui
