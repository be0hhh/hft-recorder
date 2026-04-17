# hft-recorder — Logging & Metrics

Defines the spdlog layout (what gets logged, where, at which level) and the full Prometheus
metric surface (every counter, gauge, summary, their labels, their emission points).

---

## Logging

### Library

`spdlog` (header-only via `find_package` or `FetchContent`). Used through the wrapper at
`src/core/log/Log.hpp` — **never** `#include <spdlog/spdlog.h>` directly in other files
(keeps swap-out possible). See `CODING_STYLE.md` § "Logging".

### Sinks

One async logger per process; two sinks attached:

| Sink | Level | Rotation |
|---|---|---|
| stdout (colour) | `info` and above | none |
| `logs/hft-recorder-%Y%m%d.log` | `trace` and above | 100 MB × 7 files (daily rolling + size cap) |

Async queue size: 8192. Thread count: 1. Overflow policy: `block` (back-pressure the logger
rather than drop lines — logs are diagnostic, loss defeats the purpose).

### Category loggers

Each subsystem gets a named logger to keep `grep` useful:

| Logger name | Owned by |
|---|---|
| `producer.trades` | trade producer thread |
| `producer.bookticker` | bookTicker producer |
| `producer.depth` | depth@0ms callback producer |
| `producer.snapshot` | snapshot REST poller |
| `writer.trades` | trades writer thread |
| `writer.bookticker` | bookTicker writer |
| `writer.depth` | depth writer |
| `writer.snapshot` | snapshot writer |
| `control` | watchdog / signal / metrics push thread |
| `main` | bootstrap, config load, thread spawn, shutdown |
| `codec` | encode / decode paths (bench only; silent during recording) |
| `bench` | bench tool top-level |

Loggers are created in `initLogging()` and stored in a `flat_hash_map<string_view, std::shared_ptr<spdlog::logger>>`. `Log::get(category)` returns by name — fallback to `main` if unknown.

### Level policy

| Level | Typical events |
|---|---|
| `trace` | per-event ring push / pop, per-byte codec diagnostics (disabled by default, enabled via env `LOG_LEVEL=trace` for forensic sessions only) |
| `debug` | per-block header fields on flush, per-second heartbeat, subscription payload sent |
| `info` | startup banner, thread bind success, block flushed with reason, periodic summary (events + bytes per minute), graceful shutdown start/finish |
| `warn` | SPSC drop, WS timeout/reconnect, CRC mismatch at read, CPU affinity failed, clock skew, snapshot fetch failed, slow fsync |
| `error` | disk I/O failure, `runSubscribe*` returned false, config load failed, watchdog stalled, unrecoverable codec corruption (bench only) |
| `critical` | reserved for `initBuildDispatch` failure, library not loaded — i.e. preconditions that make progress impossible |

Every `warn` and `error` line **must** contain the symbol + exchange + stream in the format
`[sym=BTCUSDT ex=binance stream=trades]` so ops can filter.

### Log format

```
[2026-04-17T15:23:41.123+00:00] [producer.trades] [warn] [sym=BTCUSDT ex=binance stream=trades] WS disconnect detected; reconnect #3 in 2s
```

Pattern string (spdlog):
`[%Y-%m-%dT%H:%M:%S.%e%z] [%n] [%^%l%$] %v`

Log lines never exceed one line; newlines in rendered values are replaced with spaces.

### What NOT to log

- Full packet bodies — too big, potential PII concern.
- Stack traces during normal shutdown.
- API keys — redacted by the config loader before logging.
- Event-level numeric values during capture (only on `trace`).

---

## Prometheus metrics

Emitted from `src/core/metrics/Metrics.cpp`. Registered with `prometheus::Registry`; the
control thread runs the `prometheus::Exposer` or push loop depending on configuration.

If `PROMETHEUS_PUSHGATEWAY_URL` is set: `prometheus::Gateway` pushes every
`kMetricsPushIntervalSec` (default 10 s). Otherwise metrics are held in-process and exposed
on `http://0.0.0.0:9500/metrics` (for scraping mode).

All metric names begin with `hft_recorder_` (recorder side) or `hft_recorder_bench_` (bench).

### Recorder metrics

| Name | Type | Labels | Incremented / set when |
|---|---|---|---|
| `hft_recorder_events_captured_total` | counter | `stream`, `symbol`, `exchange` | every successful `tryPop` from `CxetStream` or delta callback |
| `hft_recorder_events_dropped_total` | counter | `stream`, `symbol`, `reason` | SPSC full / gap detected / out-of-order; `reason ∈ {spsc_full, gap, oos}` |
| `hft_recorder_bytes_written_total` | counter | `stream` | after every successful block `pwrite` (header + payload bytes) |
| `hft_recorder_blocks_flushed_total` | counter | `stream`, `reason` | on every block flush; `reason ∈ {event_count, wall_time, size, coder_reset, shutdown}` |
| `hft_recorder_block_flush_seconds` | summary | `stream`, `quantile` (0.5, 0.95, 0.99) | per-block wall-clock from first event to pwrite return |
| `hft_recorder_block_compressed_bytes` | histogram | `stream`, bucket sizes 1KB..256KB | after each block flush |
| `hft_recorder_spsc_depth` | gauge | `stream` | sampled every 1 s by control thread (approxSize) |
| `hft_recorder_pwrite_errors_total` | counter | `stream`, `errno` | on `pwrite` returning -1 |
| `hft_recorder_fsync_duration_seconds` | summary | `stream`, `quantile` | every fsync call |
| `hft_recorder_coder_resets_total` | counter | `stream`, `reason` | every CODER_RESET emission; `reason ∈ {periodic, ws_disconnect, updateid_gap, manual}` |
| `hft_recorder_ws_reconnects_total` | counter | `stream` | signalled by producer on library-reported reconnect |
| `hft_recorder_ws_rtt_seconds` | summary | `stream`, `quantile` | ping/pong RTT from library diagnostics callback (when available) |
| `hft_recorder_snapshot_fetches_total` | counter | `outcome` (`ok`/`fail`) | every `runGetOrderBookByConfig` |
| `hft_recorder_snapshot_depth` | gauge | `side` (`bid`/`ask`) | on each successful snapshot |
| `hft_recorder_clock_skew_events_total` | counter | `stream` | when `ts < last_ts` |
| `hft_recorder_uptime_seconds` | gauge | — | set by control thread every tick |
| `hft_recorder_cpu_affinity_ok` | gauge (0/1) | `thread` | set at thread start; 1 = pinned, 0 = failed |
| `hft_recorder_watchdog_stalled` | gauge (0/1) | `thread` | 1 when no progress for > 30 s |

### Bench metrics

Emitted once per block processed during bench.

| Name | Type | Labels | Meaning |
|---|---|---|---|
| `hft_recorder_bench_ratio` | gauge | `codec`, `stream` | `bytes_out / bytes_in` for the last block (overwritten each block); **final** value after run = whole-file ratio |
| `hft_recorder_bench_ratio_whole` | gauge | `codec`, `stream` | whole-file ratio, set once at end |
| `hft_recorder_bench_encode_ns` | summary | `codec`, `stream`, `quantile` | per-block encode wall ns (RDTSC-derived) |
| `hft_recorder_bench_decode_ns` | summary | `codec`, `stream`, `quantile` | per-block decode wall ns |
| `hft_recorder_bench_encode_mb_per_sec` | gauge | `codec`, `stream` | `bytes_in / encode_ns * 1e9 / 1e6` whole-file |
| `hft_recorder_bench_decode_mb_per_sec` | gauge | `codec`, `stream` | symmetric |
| `hft_recorder_bench_blocks_processed_total` | counter | `codec`, `stream` | |
| `hft_recorder_bench_roundtrip_failed_total` | counter | `codec`, `stream` | increments when `--roundtrip` catches a mismatch (should be 0) |
| `hft_recorder_bench_peak_memory_bytes` | gauge | `codec`, `stream` | RSS peak during the run (sampled from `/proc/self/status`) |

### Build info

`hft_recorder_build_info{version="…", commit="…", cxet_version="…"}` gauge = 1. Set once at startup.

---

## Label cardinality budget

Recorder:
- `stream` ∈ {trades, bookticker, depth, snapshot} → 4 values.
- `symbol` ∈ one value per running process (one symbol per recorder). Total cardinality = 4.
- `reason` labels are small closed enums.

Bench:
- `codec` = 7, `stream` = 4 → 28 series per metric. Well under Prometheus label budget.

**Do not** emit `updateId` or `ts` as labels. Those go in the `.cxrec` file itself, never into
Prometheus.

---

## Emission rules

- Counters are **monotonic**; never reset except on process restart. Survive Prometheus
  scrape gaps.
- Gauges are sampled snapshots; `spsc_depth`, `uptime_seconds`, etc. use "last-write-wins".
- Summaries / histograms emit on every observation. Quantiles computed client-side.
- Timer observations use `std::chrono::steady_clock::now()` or RDTSC-converted ns.
- Allocations on the metrics path: only during registration (startup). Observation points
  use `Counter::Increment(1)` which is branch + atomic add only.

---

## Grafana dashboard (future)

`grafana/dashboards/hft-recorder.json` shows (non-exhaustive):

- **Recorder**: events/sec per stream; drops/sec; blocks/sec; bytes on disk; SPSC depth;
  fsync p99; WS reconnects; watchdog state.
- **Bench**: the 28-cell ratio grid; encode/decode MB/s bar chart; peak RSS per codec.

See `BENCHMARK_PLAN.md` for the research layout.

---

## What the bench run pushes

Single-shot push at end of run:

```
hft_recorder_bench_ratio_whole{codec="rans_ctx8",stream="trades"} 0.38
hft_recorder_bench_encode_mb_per_sec{codec="rans_ctx8",stream="trades"} 420
hft_recorder_bench_decode_mb_per_sec{codec="rans_ctx8",stream="trades"} 1430
hft_recorder_bench_peak_memory_bytes{codec="rans_ctx8",stream="trades"} 12582912
hft_recorder_bench_blocks_processed_total{codec="rans_ctx8",stream="trades"} 3421
hft_recorder_bench_roundtrip_failed_total{codec="rans_ctx8",stream="trades"} 0
```

If `--roundtrip` finds a mismatch the bench exits non-zero AFTER pushing metrics (so Grafana
can display the failure count).

---

## References

- `CODING_STYLE.md` § "Logging" — wrapper rule.
- `ERROR_HANDLING_AND_GAPS.md` — which metric fires for each incident row.
- `BENCHMARK_PLAN.md` — 28-cell research grid.
- `CONFIG_AND_CLI.md` — env variables that enable / disable push.
