#include "RecorderTuiShard.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "Corpus/Recordings/RecordingDiscovery.hpp"

namespace hftrec::tui {

namespace {

constexpr int kDefaultBySymbolMaxActiveJobsPerShard = 4;

std::string venueGroupKey(const RecorderTuiJob& job) {
    return job.exchange + "|" + job.market;
}

}  // namespace

std::vector<RecorderTuiPreset> splitPresetIntoShards(const RecorderTuiPreset& preset,
                                                     int shardCount,
                                                     int maxActiveJobsPerShard,
                                                     RecorderTuiShardGrouping grouping) {
    std::vector<std::pair<std::string, std::vector<RecorderTuiJob>>> groups;
    for (const auto& job : preset.jobs) {
        if (grouping == RecorderTuiShardGrouping::ByJob) {
            groups.push_back({job.name, {job}});
            continue;
        }

        std::string groupKey;
        if (grouping == RecorderTuiShardGrouping::ByVenue) {
            groupKey = venueGroupKey(job);
        } else {
            groupKey = recordings::recordingFolderSymbol(job.exchange, job.market, job.symbol);
            if (groupKey.empty()) groupKey = job.symbol;
        }
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& item) { return item.first == groupKey; });
        if (it == groups.end()) {
            groups.push_back({std::move(groupKey), {job}});
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

int clampRecorderTuiMaxActiveShards(int requested, int shardCount) noexcept {
    const int count = std::max(1, shardCount);
    return std::max(1, std::min(std::max(1, requested), count));
}

int defaultRecorderTuiMaxActiveJobsPerShard(const RecorderTuiPreset& preset,
                                            RecorderTuiShardGrouping grouping) noexcept {
    const int activeJobs = std::max(1, preset.maxActiveJobs);
    if (grouping == RecorderTuiShardGrouping::ByJob || grouping == RecorderTuiShardGrouping::ByVenue) return 1;
    return std::max(1, std::min(activeJobs, kDefaultBySymbolMaxActiveJobsPerShard));
}

int defaultRecorderTuiMaxActiveShards(const RecorderTuiPreset& preset,
                                      RecorderTuiShardGrouping grouping,
                                      int shardCount) noexcept {
    const int activeJobs = std::max(1, preset.maxActiveJobs);
    const int requested = grouping == RecorderTuiShardGrouping::ByJob || grouping == RecorderTuiShardGrouping::ByVenue
        ? activeJobs
        : shardCount;
    return clampRecorderTuiMaxActiveShards(requested, shardCount);
}

RecorderTuiShardLaunchDecision chooseQueuedShardLaunches(const std::vector<RecorderTuiShardLaunchState>& states,
                                                         int maxActiveShards) {
    RecorderTuiShardLaunchDecision decision{};
    const int limit = clampRecorderTuiMaxActiveShards(maxActiveShards, static_cast<int>(states.size()));
    for (const auto& state : states) {
        if (state.launchStarted && !state.exited) ++decision.active;
        if (!state.launchStarted && !state.exited && !state.stopRequested) ++decision.queued;
    }

    int slots = std::max(0, limit - decision.active);
    if (slots == 0) return decision;
    decision.indices.reserve(static_cast<std::size_t>(slots));
    for (std::size_t i = 0; i < states.size() && slots > 0; ++i) {
        const auto& state = states[i];
        if (state.launchStarted || state.exited || state.stopRequested) continue;
        decision.indices.push_back(i);
        --slots;
    }
    return decision;
}

void markQueuedShardLaunchesStopped(std::vector<RecorderTuiShardLaunchState>& states) noexcept {
    for (auto& state : states) {
        if (state.launchStarted || state.exited) continue;
        state.stopRequested = true;
        state.exited = true;
    }
}

}  // namespace hftrec::tui
