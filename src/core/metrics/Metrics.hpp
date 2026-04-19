#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hftrec::metrics {

struct StreamRuntimeMetrics {
    std::string stream{};
    std::uint64_t eventsCapturedTotal{0};
    std::uint64_t eventsDroppedTotal{0};
    std::uint64_t bytesWrittenTotal{0};
    std::uint64_t writeErrorsTotal{0};
    std::uint64_t snapshotFetchFailuresTotal{0};
    std::uint64_t wsReconnectsTotal{0};
    std::uint64_t lastEventTsNs{0};
    std::uint64_t lastWriteTsNs{0};
};

struct ReplayRuntimeMetrics {
    std::uint64_t sessionLoadsTotal{0};
    std::uint64_t rowsLoadedTotal{0};
    std::uint64_t replaySeekCountTotal{0};
    std::uint64_t replayParseFailuresTotal{0};
    std::uint64_t lastSessionLoadNs{0};
};

struct ValidationRuntimeMetrics {
    std::uint64_t runsTotal{0};
    std::uint64_t eventsTotal{0};
    std::uint64_t eventsExactMatch{0};
    std::uint64_t eventsMismatch{0};
    std::uint64_t lastRunNs{0};
};

struct LabRuntimeMetrics {
    std::uint64_t runsTotal{0};
    std::uint64_t inputBytes{0};
    std::uint64_t outputBytes{0};
    std::uint64_t encodeNs{0};
    std::uint64_t decodeNs{0};
    std::uint64_t roundtripFailedTotal{0};
};

struct MetricsSnapshot {
    std::vector<StreamRuntimeMetrics> streams{};
    ReplayRuntimeMetrics replay{};
    ValidationRuntimeMetrics validation{};
    LabRuntimeMetrics lab{};
};

void init() noexcept;
void shutdown() noexcept;

void incEventsCaptured(std::string_view stream) noexcept;
void incEventsDropped(std::string_view stream, std::string_view reason) noexcept;
void addBytesWritten(std::string_view stream, std::uint64_t n) noexcept;
void incBlocksFlushed(std::string_view stream, std::string_view reason) noexcept;

void recordCaptureEvent(std::string_view stream,
                        std::uint64_t eventTsNs,
                        std::uint64_t bytesWritten,
                        std::uint64_t writeTsNs) noexcept;
void recordCaptureWriteError(std::string_view stream) noexcept;
void recordSnapshotFetchFailure(std::string_view stream) noexcept;
void addWsReconnects(std::string_view stream, std::uint64_t n) noexcept;

void recordReplayLoad(std::size_t rowsLoaded, std::uint64_t loadNs) noexcept;
void recordReplaySeek() noexcept;
void recordReplayParseFailure(std::string_view source) noexcept;

void recordValidationRun(std::size_t eventsTotal,
                         std::size_t eventsExactMatch,
                         std::size_t eventsMismatch,
                         std::uint64_t runNs) noexcept;

void recordLabRun(std::size_t inputBytes,
                  std::size_t outputBytes,
                  std::uint64_t encodeNs,
                  std::uint64_t decodeNs,
                  bool roundtripFailed) noexcept;

MetricsSnapshot snapshot() noexcept;

}  // namespace hftrec::metrics
