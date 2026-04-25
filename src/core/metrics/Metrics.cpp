#include "core/metrics/Metrics.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <mutex>
#include <string>

#include "core/common/Timing.hpp"

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

std::uint64_t maxOf(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs > rhs ? lhs : rhs;
}

void addLatency(std::uint64_t ns,
                std::uint64_t& count,
                std::uint64_t& total,
                std::uint64_t& max) noexcept {
    ++count;
    total += ns;
    max = maxOf(max, ns);
}

std::uint64_t parseStatusBytes(std::string_view key) noexcept {
    std::ifstream in{"/proc/self/status"};
    if (!in) return 0;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss{line};
        std::string name;
        std::uint64_t valueKb = 0;
        std::string unit;
        if (!(iss >> name >> valueKb >> unit)) continue;
        if (name == key) return valueKb * 1024ull;
    }
    return 0;
}

}  // namespace

void init() noexcept {
    hftrec::timing::ensureCalibrated();
    std::lock_guard<std::mutex> lock(stateMutex());
    (void)streamMetrics_("trades");
    (void)streamMetrics_("bookticker");
    (void)streamMetrics_("depth");
    (void)streamMetrics_("snapshot");
}

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

void setLiveRows(std::uint64_t trades,
                 std::uint64_t bookTickers,
                 std::uint64_t depths,
                 std::uint64_t snapshots) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& live = state().snapshot.live;
    live.tradesRows = trades;
    live.bookTickerRows = bookTickers;
    live.depthRows = depths;
    live.snapshotRows = snapshots;
}

void recordLivePoll(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& live = state().snapshot.live;
    addLatency(ns, live.pollCountTotal, live.pollNsTotal, live.pollNsMax);
}

void recordLiveMaterialize(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& live = state().snapshot.live;
    addLatency(ns, live.materializeCountTotal, live.materializeNsTotal, live.materializeNsMax);
}

void recordLiveJsonTailParse(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& live = state().snapshot.live;
    addLatency(ns, live.jsonTailParseCountTotal, live.jsonTailParseNsTotal, live.jsonTailParseNsMax);
}

void recordGuiPaint(std::uint64_t ns, std::uint64_t frameEndTsc) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    ++gui.frameTotal;
    gui.paintNsTotal += ns;
    gui.paintNsMax = maxOf(gui.paintNsMax, ns);
    if (gui.lastFrameTsc != 0u && frameEndTsc > gui.lastFrameTsc) {
        hftrec::timing::Tick start{};
        hftrec::timing::Tick end{};
        start.raw = gui.lastFrameTsc;
        end.raw = frameEndTsc;
        const auto deltaNs = hftrec::timing::deltaNs(start, end);
        gui.fps = deltaNs.raw > 0u ? 1000000000ull / deltaNs.raw : 0u;
    }
    gui.lastFrameTsc = frameEndTsc;
}

void recordGuiSnapshotBuild(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.snapshotBuildCountTotal, gui.snapshotBuildNsTotal, gui.snapshotBuildNsMax);
}

void recordGuiLiveSnapshotBuild(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.liveSnapshotBuildCountTotal, gui.liveSnapshotBuildNsTotal, gui.liveSnapshotBuildNsMax);
}

void recordGuiLiveSnapshotDraw(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.liveSnapshotDrawCountTotal, gui.liveSnapshotDrawNsTotal, gui.liveSnapshotDrawNsMax);
}

void recordGuiRenderOrderbook(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.renderOrderbookCountTotal, gui.renderOrderbookNsTotal, gui.renderOrderbookNsMax);
}

void recordGuiRenderBookTicker(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.renderBookTickerCountTotal, gui.renderBookTickerNsTotal, gui.renderBookTickerNsMax);
}

void recordGuiRenderTrades(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.renderTradesCountTotal, gui.renderTradesNsTotal, gui.renderTradesNsMax);
}

void recordGuiOverlayRender(std::uint64_t ns) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    addLatency(ns, gui.overlayRenderCountTotal, gui.overlayRenderNsTotal, gui.overlayRenderNsMax);
}

void incGuiLayerCacheHit() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++state().snapshot.gui.layerCacheHitTotal;
}

void incGuiLayerCacheRebuild() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    ++state().snapshot.gui.layerCacheRebuildTotal;
}

void setGuiObjectCounts(std::uint64_t orderbookSegments,
                        std::uint64_t bookTickerLines,
                        std::uint64_t bookTickerSamples,
                        std::uint64_t tradeDots) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    auto& gui = state().snapshot.gui;
    gui.orderbookSegments = orderbookSegments;
    gui.bookTickerLines = bookTickerLines;
    gui.bookTickerSamples = bookTickerSamples;
    gui.tradeDots = tradeDots;
}

MetricsSnapshot snapshot() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex());
    return state().snapshot;
}

void renderPrometheus(std::string& out) noexcept {
    const MetricsSnapshot snap = snapshot();
    const std::uint64_t rssBytes = parseStatusBytes("VmRSS:");
    const std::uint64_t vmSizeBytes = parseStatusBytes("VmSize:");
    const auto avg = [](std::uint64_t total, std::uint64_t count) noexcept -> std::uint64_t {
        return count == 0u ? 0u : total / count;
    };

    const auto appendMetric = [&out](const char* name, std::uint64_t value) noexcept {
        out += name;
        out.push_back(' ');
        out += std::to_string(value);
        out.push_back('\n');
    };

    char buf[1024];
    for (const auto& stream : snap.streams) {
        std::snprintf(buf, sizeof(buf),
                      "hftrec_stream_events_captured_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_events_dropped_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_bytes_written_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_write_errors_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_snapshot_fetch_failures_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_ws_reconnects_total{stream=\"%s\"} %llu\n"
                      "hftrec_stream_last_event_ts_ns{stream=\"%s\"} %llu\n"
                      "hftrec_stream_last_write_ts_ns{stream=\"%s\"} %llu\n",
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.eventsCapturedTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.eventsDroppedTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.bytesWrittenTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.writeErrorsTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.snapshotFetchFailuresTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.wsReconnectsTotal),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.lastEventTsNs),
                      stream.stream.c_str(), static_cast<unsigned long long>(stream.lastWriteTsNs));
        out += buf;
    }

    appendMetric("hftrec_replay_session_loads_total", snap.replay.sessionLoadsTotal);
    appendMetric("hftrec_replay_rows_loaded_total", snap.replay.rowsLoadedTotal);
    appendMetric("hftrec_replay_seek_total", snap.replay.replaySeekCountTotal);
    appendMetric("hftrec_replay_parse_failures_total", snap.replay.replayParseFailuresTotal);
    appendMetric("hftrec_replay_last_session_load_ns", snap.replay.lastSessionLoadNs);
    appendMetric("hftrec_validation_runs_total", snap.validation.runsTotal);
    appendMetric("hftrec_validation_events_total", snap.validation.eventsTotal);
    appendMetric("hftrec_validation_events_exact_match_total", snap.validation.eventsExactMatch);
    appendMetric("hftrec_validation_events_mismatch_total", snap.validation.eventsMismatch);
    appendMetric("hftrec_validation_last_run_ns", snap.validation.lastRunNs);
    appendMetric("hftrec_lab_runs_total", snap.lab.runsTotal);
    appendMetric("hftrec_lab_input_bytes_total", snap.lab.inputBytes);
    appendMetric("hftrec_lab_output_bytes_total", snap.lab.outputBytes);
    appendMetric("hftrec_lab_encode_ns_total", snap.lab.encodeNs);
    appendMetric("hftrec_lab_decode_ns_total", snap.lab.decodeNs);
    appendMetric("hftrec_lab_roundtrip_failed_total", snap.lab.roundtripFailedTotal);
    appendMetric("hftrec_process_rss_bytes", rssBytes);
    appendMetric("hftrec_process_virtual_memory_bytes", vmSizeBytes);
    appendMetric("hftrec_live_trades_rows", snap.live.tradesRows);
    appendMetric("hftrec_live_book_ticker_rows", snap.live.bookTickerRows);
    appendMetric("hftrec_live_depth_rows", snap.live.depthRows);
    appendMetric("hftrec_live_snapshot_rows", snap.live.snapshotRows);
    appendMetric("hftrec_live_poll_total", snap.live.pollCountTotal);
    appendMetric("hftrec_live_poll_ns_total", snap.live.pollNsTotal);
    appendMetric("hftrec_live_poll_ns_avg", avg(snap.live.pollNsTotal, snap.live.pollCountTotal));
    appendMetric("hftrec_live_poll_ns_max", snap.live.pollNsMax);
    appendMetric("hftrec_live_materialize_total", snap.live.materializeCountTotal);
    appendMetric("hftrec_live_materialize_ns_total", snap.live.materializeNsTotal);
    appendMetric("hftrec_live_materialize_ns_avg", avg(snap.live.materializeNsTotal, snap.live.materializeCountTotal));
    appendMetric("hftrec_live_materialize_ns_max", snap.live.materializeNsMax);
    appendMetric("hftrec_live_json_tail_parse_total", snap.live.jsonTailParseCountTotal);
    appendMetric("hftrec_live_json_tail_parse_ns_total", snap.live.jsonTailParseNsTotal);
    appendMetric("hftrec_live_json_tail_parse_ns_avg", avg(snap.live.jsonTailParseNsTotal, snap.live.jsonTailParseCountTotal));
    appendMetric("hftrec_live_json_tail_parse_ns_max", snap.live.jsonTailParseNsMax);
    appendMetric("hftrec_gui_frames_total", snap.gui.frameTotal);
    appendMetric("hftrec_gui_fps", snap.gui.fps);
    appendMetric("hftrec_gui_paint_ns_total", snap.gui.paintNsTotal);
    appendMetric("hftrec_gui_paint_ns_avg", avg(snap.gui.paintNsTotal, snap.gui.frameTotal));
    appendMetric("hftrec_gui_paint_ns_max", snap.gui.paintNsMax);
    appendMetric("hftrec_gui_snapshot_build_total", snap.gui.snapshotBuildCountTotal);
    appendMetric("hftrec_gui_snapshot_build_ns_total", snap.gui.snapshotBuildNsTotal);
    appendMetric("hftrec_gui_snapshot_build_ns_avg", avg(snap.gui.snapshotBuildNsTotal, snap.gui.snapshotBuildCountTotal));
    appendMetric("hftrec_gui_snapshot_build_ns_max", snap.gui.snapshotBuildNsMax);
    appendMetric("hftrec_gui_live_snapshot_build_total", snap.gui.liveSnapshotBuildCountTotal);
    appendMetric("hftrec_gui_live_snapshot_build_ns_total", snap.gui.liveSnapshotBuildNsTotal);
    appendMetric("hftrec_gui_live_snapshot_build_ns_avg", avg(snap.gui.liveSnapshotBuildNsTotal, snap.gui.liveSnapshotBuildCountTotal));
    appendMetric("hftrec_gui_live_snapshot_build_ns_max", snap.gui.liveSnapshotBuildNsMax);
    appendMetric("hftrec_gui_live_snapshot_draw_total", snap.gui.liveSnapshotDrawCountTotal);
    appendMetric("hftrec_gui_live_snapshot_draw_ns_total", snap.gui.liveSnapshotDrawNsTotal);
    appendMetric("hftrec_gui_live_snapshot_draw_ns_avg", avg(snap.gui.liveSnapshotDrawNsTotal, snap.gui.liveSnapshotDrawCountTotal));
    appendMetric("hftrec_gui_live_snapshot_draw_ns_max", snap.gui.liveSnapshotDrawNsMax);
    appendMetric("hftrec_gui_render_orderbook_total", snap.gui.renderOrderbookCountTotal);
    appendMetric("hftrec_gui_render_orderbook_ns_total", snap.gui.renderOrderbookNsTotal);
    appendMetric("hftrec_gui_render_orderbook_ns_avg", avg(snap.gui.renderOrderbookNsTotal, snap.gui.renderOrderbookCountTotal));
    appendMetric("hftrec_gui_render_orderbook_ns_max", snap.gui.renderOrderbookNsMax);
    appendMetric("hftrec_gui_render_book_ticker_total", snap.gui.renderBookTickerCountTotal);
    appendMetric("hftrec_gui_render_book_ticker_ns_total", snap.gui.renderBookTickerNsTotal);
    appendMetric("hftrec_gui_render_book_ticker_ns_avg", avg(snap.gui.renderBookTickerNsTotal, snap.gui.renderBookTickerCountTotal));
    appendMetric("hftrec_gui_render_book_ticker_ns_max", snap.gui.renderBookTickerNsMax);
    appendMetric("hftrec_gui_render_trades_total", snap.gui.renderTradesCountTotal);
    appendMetric("hftrec_gui_render_trades_ns_total", snap.gui.renderTradesNsTotal);
    appendMetric("hftrec_gui_render_trades_ns_avg", avg(snap.gui.renderTradesNsTotal, snap.gui.renderTradesCountTotal));
    appendMetric("hftrec_gui_render_trades_ns_max", snap.gui.renderTradesNsMax);
    appendMetric("hftrec_gui_overlay_render_total", snap.gui.overlayRenderCountTotal);
    appendMetric("hftrec_gui_overlay_render_ns_total", snap.gui.overlayRenderNsTotal);
    appendMetric("hftrec_gui_overlay_render_ns_avg", avg(snap.gui.overlayRenderNsTotal, snap.gui.overlayRenderCountTotal));
    appendMetric("hftrec_gui_overlay_render_ns_max", snap.gui.overlayRenderNsMax);
    appendMetric("hftrec_gui_layer_cache_hit_total", snap.gui.layerCacheHitTotal);
    appendMetric("hftrec_gui_layer_cache_rebuild_total", snap.gui.layerCacheRebuildTotal);
    appendMetric("hftrec_gui_orderbook_segments", snap.gui.orderbookSegments);
    appendMetric("hftrec_gui_book_ticker_lines", snap.gui.bookTickerLines);
    appendMetric("hftrec_gui_book_ticker_samples", snap.gui.bookTickerSamples);
    appendMetric("hftrec_gui_trade_dots", snap.gui.tradeDots);
    appendMetric("hftrec_gui_last_frame_tsc", snap.gui.lastFrameTsc);
}
}  // namespace hftrec::metrics


