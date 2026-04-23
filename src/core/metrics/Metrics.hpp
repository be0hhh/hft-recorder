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

struct LiveRuntimeMetrics {
    std::uint64_t tradesRows{0};
    std::uint64_t bookTickerRows{0};
    std::uint64_t depthRows{0};
    std::uint64_t snapshotRows{0};
    std::uint64_t pollCountTotal{0};
    std::uint64_t pollNsTotal{0};
    std::uint64_t pollNsMax{0};
    std::uint64_t materializeCountTotal{0};
    std::uint64_t materializeNsTotal{0};
    std::uint64_t materializeNsMax{0};
    std::uint64_t jsonTailParseCountTotal{0};
    std::uint64_t jsonTailParseNsTotal{0};
    std::uint64_t jsonTailParseNsMax{0};
};

struct GuiRuntimeMetrics {
    std::uint64_t frameTotal{0};
    std::uint64_t fps{0};
    std::uint64_t paintNsTotal{0};
    std::uint64_t paintNsMax{0};
    std::uint64_t snapshotBuildNsTotal{0};
    std::uint64_t snapshotBuildNsMax{0};
    std::uint64_t snapshotBuildCountTotal{0};
    std::uint64_t liveSnapshotBuildNsTotal{0};
    std::uint64_t liveSnapshotBuildNsMax{0};
    std::uint64_t liveSnapshotBuildCountTotal{0};
    std::uint64_t liveSnapshotDrawNsTotal{0};
    std::uint64_t liveSnapshotDrawNsMax{0};
    std::uint64_t liveSnapshotDrawCountTotal{0};
    std::uint64_t renderOrderbookNsTotal{0};
    std::uint64_t renderOrderbookNsMax{0};
    std::uint64_t renderOrderbookCountTotal{0};
    std::uint64_t renderBookTickerNsTotal{0};
    std::uint64_t renderBookTickerNsMax{0};
    std::uint64_t renderBookTickerCountTotal{0};
    std::uint64_t renderTradesNsTotal{0};
    std::uint64_t renderTradesNsMax{0};
    std::uint64_t renderTradesCountTotal{0};
    std::uint64_t overlayRenderNsTotal{0};
    std::uint64_t overlayRenderNsMax{0};
    std::uint64_t overlayRenderCountTotal{0};
    std::uint64_t layerCacheHitTotal{0};
    std::uint64_t layerCacheRebuildTotal{0};
    std::uint64_t orderbookSegments{0};
    std::uint64_t bookTickerLines{0};
    std::uint64_t bookTickerSamples{0};
    std::uint64_t tradeDots{0};
    std::uint64_t lastFrameTsc{0};
};

struct MetricsSnapshot {
    std::vector<StreamRuntimeMetrics> streams{};
    ReplayRuntimeMetrics replay{};
    ValidationRuntimeMetrics validation{};
    LabRuntimeMetrics lab{};
    LiveRuntimeMetrics live{};
    GuiRuntimeMetrics gui{};
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

void setLiveRows(std::uint64_t trades,
                 std::uint64_t bookTickers,
                 std::uint64_t depths,
                 std::uint64_t snapshots) noexcept;
void recordLivePoll(std::uint64_t ns) noexcept;
void recordLiveMaterialize(std::uint64_t ns) noexcept;
void recordLiveJsonTailParse(std::uint64_t ns) noexcept;

void recordGuiPaint(std::uint64_t ns, std::uint64_t frameEndTsc) noexcept;
void recordGuiSnapshotBuild(std::uint64_t ns) noexcept;
void recordGuiLiveSnapshotBuild(std::uint64_t ns) noexcept;
void recordGuiLiveSnapshotDraw(std::uint64_t ns) noexcept;
void recordGuiRenderOrderbook(std::uint64_t ns) noexcept;
void recordGuiRenderBookTicker(std::uint64_t ns) noexcept;
void recordGuiRenderTrades(std::uint64_t ns) noexcept;
void recordGuiOverlayRender(std::uint64_t ns) noexcept;
void incGuiLayerCacheHit() noexcept;
void incGuiLayerCacheRebuild() noexcept;
void setGuiObjectCounts(std::uint64_t orderbookSegments,
                        std::uint64_t bookTickerLines,
                        std::uint64_t bookTickerSamples,
                        std::uint64_t tradeDots) noexcept;

MetricsSnapshot snapshot() noexcept;
void renderPrometheus(std::string& out) noexcept;

}  // namespace hftrec::metrics
