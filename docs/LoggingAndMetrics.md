# hft-recorder — Logging

Defines the logging layout. Monitoring exporters, push collectors and GUI
performance profiling have been removed. Offline compression and Backtest
reports remain part of the research workflow.

---

## Logging

### Library

`spdlog` (header-only via `find_package` or `FetchContent`). Used through the wrapper at
`src/Runtime/src/Common/Log.hpp` — **never** `#include <spdlog/spdlog.h>` directly in other files
(keeps swap-out possible). See `CodingStyle.md` § "Logging".

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
| `control` | watchdog / signal handling |
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
