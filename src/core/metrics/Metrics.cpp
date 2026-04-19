#include "core/metrics/Metrics.hpp"

#include <algorithm>
#include <mutex>

namespace hftrec::metrics {

namespace {

struct MetricsState {
    MetricsSnapshot snapshot{};
};

MetricsState& state() noexcept {
    static MetricsState metricsState{};
    return metricsState;
}

std::mutex& stateMutex() noexcept {
    static std::mutex mutex{};
    return mutex;
}

StreamRuntimeMetrics& streamMetrics_(std::string_view stream) {
    auto& streams = state().snapshot.streams;
    const auto it = std::find_if(streams.begin(), streams.end(),
                                 [stream](const StreamRuntimeMetrics& candidate) noexcept {
                                     return candidate.stream == stream;
                                 });
    if (it != streams.end()) {
        return *it;
    }
    streams.push_back(StreamRuntimeMetrics{std::string{stream}});
    return streams.back();
}

}  // namespace

void init() noexcept {}

void shutdown() noexcept {}

void incEventsCaptured(std::string_view stream) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++streamMetrics_(stream).eventsCapturedTotal;
}

void incEventsDropped(std::string_view stream, std::string_view) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++streamMetrics_(stream).eventsDroppedTotal;
}

void addBytesWritten(std::string_view stream, std::uint64_t n) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    streamMetrics_(stream).bytesWrittenTotal += n;
}

void incBlocksFlushed(std::string_view, std::string_view) noexcept {}

void recordCaptureEvent(std::string_view stream,
                        std::uint64_t eventTsNs,
                        std::uint64_t bytesWritten,
                        std::uint64_t writeTsNs) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& metrics = streamMetrics_(stream);
    ++metrics.eventsCapturedTotal;
    metrics.bytesWrittenTotal += bytesWritten;
    metrics.lastEventTsNs = eventTsNs;
    metrics.lastWriteTsNs = writeTsNs;
}

void recordCaptureWriteError(std::string_view stream) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++streamMetrics_(stream).writeErrorsTotal;
}

void recordSnapshotFetchFailure(std::string_view stream) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++streamMetrics_(stream).snapshotFetchFailuresTotal;
}

void addWsReconnects(std::string_view stream, std::uint64_t n) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    streamMetrics_(stream).wsReconnectsTotal += n;
}

void recordReplayLoad(std::size_t rowsLoaded, std::uint64_t loadNs) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++state().snapshot.replay.sessionLoadsTotal;
    state().snapshot.replay.rowsLoadedTotal += static_cast<std::uint64_t>(rowsLoaded);
    state().snapshot.replay.lastSessionLoadNs = loadNs;
}

void recordReplaySeek() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++state().snapshot.replay.replaySeekCountTotal;
}

void recordReplayParseFailure(std::string_view) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++state().snapshot.replay.replayParseFailuresTotal;
}

void recordValidationRun(std::size_t eventsTotal,
                         std::size_t eventsExactMatch,
                         std::size_t eventsMismatch,
                         std::uint64_t runNs) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& metrics = state().snapshot.validation;
    ++metrics.runsTotal;
    metrics.eventsTotal += static_cast<std::uint64_t>(eventsTotal);
    metrics.eventsExactMatch += static_cast<std::uint64_t>(eventsExactMatch);
    metrics.eventsMismatch += static_cast<std::uint64_t>(eventsMismatch);
    metrics.lastRunNs = runNs;
}

void recordLabRun(std::size_t inputBytes,
                  std::size_t outputBytes,
                  std::uint64_t encodeNs,
                  std::uint64_t decodeNs,
                  bool roundtripFailed) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& metrics = state().snapshot.lab;
    ++metrics.runsTotal;
    metrics.inputBytes += static_cast<std::uint64_t>(inputBytes);
    metrics.outputBytes += static_cast<std::uint64_t>(outputBytes);
    metrics.encodeNs += encodeNs;
    metrics.decodeNs += decodeNs;
    if (roundtripFailed) {
        ++metrics.roundtripFailedTotal;
    }
}

MetricsSnapshot snapshot() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    return state().snapshot;
}

}  // namespace hftrec::metrics
