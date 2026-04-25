# hft-recorder — Error Handling & Gaps

Defines **every** failure mode observable at runtime, how to detect it, and which file-format
marker (if any) must be emitted. The recorder must never silently drop data without recording
a gap in the `.cxrec` stream so that downstream bench/analysis tools can account for it.

---

## Philosophy

- **No exceptions.** Library compiled `-fno-exceptions`; recorder follows suit. Every fallible
  operation returns `Status` (see `API_CONTRACTS.md`).
- **No partial blocks on disk.** A block is either fully written (header + payload + CRC) or not
  at all. The `BlockWriter` uses a staging buffer and one atomic `pwrite` per block.
- **Gap > silent drop.** If we cannot guarantee a contiguous event sequence, the gap is
  **explicit** in the file: `GAP_MARKER` event + optional `CODER_RESET` block flag.
- **Fail loud on startup, soft at runtime.** Missing `.so`, invalid config, permission denied →
  exit with code. WS disconnect mid-run → log + reconnect, continue recording.

---

## Incident table

| # | Incident | Detection | Response | Emitted marker |
|---|---|---|---|---|
| 1 | WS disconnect (aggTrade / bookTicker) | `CxetStream::tryPop()` returns `false` for > 5 s AND library's internal reconnect counter increments | Log `warn`; flush current block with `CODER_RESET` flag; continue popping; library re-opens WS with backoff. | `BlockHeader.flags |= kFlagCoderReset` |
| 2 | WS disconnect (depth@0ms) | Callback not invoked for > 5 s OR library returns `false` from `runSubscribeOrderBookRuntimeByConfig` | Callback thread exits; control thread relaunches the producer after 1 s; writer emits `CODER_RESET` on next block. | `CODER_RESET` + `GAP_MARKER` event |
| 3 | depth@0ms updateId gap (expected N, got N+Δ, Δ>1) | Delta encoder sees `delta_updateId > 1` | Flush current block with `CODER_RESET`; insert a `GAP_MARKER` event; schedule a fresh REST snapshot fetch on next poll tick; continue. | `CODER_RESET` + `GAP_MARKER` |
| 4 | SPSC ring full (producer faster than writer) | `ring.tryPush()` returns `false` | Drop the event; increment `events_dropped_total{reason=spsc_full}`; mark a pending gap flag; writer emits `GAP_MARKER` on next block boundary. | `GAP_MARKER` |
| 5 | Writer I/O error (`pwrite` returns `-1`, `ENOSPC` / `EIO`) | `errno` check after `pwrite` | Log `error`; set writer-local `ioFailed = true`; do not attempt further writes for this stream; control thread captures `pwrite_errors_total` counter; other 3 writers keep running. | no marker (file is closed at next fsync point) |
| 6 | Block CRC32C mismatch at read time (bench tool) | `crc32c(payload) != BlockHeader.crc32c` | Log `warn`; skip forward to the next `CODER_RESET` block; count `blocks_skipped_crc_total`. | read-side only |
| 7 | File magic / version mismatch on open | `FileHeader.magic != kFileMagic` or `version > kFileVersion` | Refuse to open; return `Status::BadMagic`. | N/A |
| 8 | File header CRC mismatch on open | `crc32c(header[0..59]) != header.crc32c` | Refuse to open; return `Status::CrcMismatch`. | N/A |
| 9 | Library missing at runtime (`libcxet_lib.so` unresolved) | Dynamic linker fails before `main()` | Process exits with code 127 from the loader (cannot recover in-process). | N/A |
| 10 | `initBuildDispatch()` not called | Every `runSubscribe…` returns `false` on first call | Treat as fatal configuration error; exit with code 2. **Prevent** by always calling it first thing in `main()`. | N/A |
| 11 | Clock skew (`ev.ts < last_ts`) | Delta encoder sees negative `ts` delta | Log `warn` once per stream; encode `delta_ts = 0`; continue. WHY: Binance re-orders occasionally when multiple matches settle at same engine tick. | no marker |
| 12 | SIGTERM / SIGINT | Signal handler sets `g_stop` | Producers finish current `tryPop`; writers finalize current block (pad payload if needed), `fsync()`, `close(fd)`; exit 0 within 2 s. | normal EOF block |
| 13 | SIGKILL / power loss | N/A | File may end mid-block. Bench tool scans forward on open: last block is truncated past the payload size declared in its header → tool rewinds to last fully-valid block. | truncated tail |
| 14 | Permission denied on output dir | `openat(path, O_WRONLY | O_CREAT)` fails | Exit with code 2; log file path + errno. | N/A |
| 15 | Disk fill mid-session (`ENOSPC`) | See incident #5 | Stop one writer, keep others. Metrics + log let user notice. | N/A |
| 16 | Snapshot REST 5xx / timeout | `runGetOrderBookByConfig` returns `false` | Log `warn`; skip this poll cycle; retry in 60 s. Does NOT break the depth@0ms stream. | no marker (snapshot is separate file) |
| 17 | Snapshot schema change (unexpected fields) | Library parse returns `false` | Same as #16; user is expected to update CXETCPP and rebuild. | no marker |
| 18 | Config `.env` missing / malformed | `loadEnv()` returns `Status::InvalidArgument` | Exit code 2 with human-readable message pointing to `.env.example`. | N/A |
| 19 | CPU affinity request fails | `cxet::os::setThisThreadAffinity` returns `false` | Log `warn`; continue **unpinned**. Metrics flag `cpu_affinity_ok{thread=…} = 0`. Not fatal. | N/A |
| 20 | `fsync` takes > 1 s | Writer timestamps `fsync` | Log `warn`; record `fsync_duration_seconds_bucket`. Non-fatal but surfaces slow disk. | no marker |

---

## GAP_MARKER — on-disk encoding

Reusing the Binance-native semantics documented in `DELTA_ENCODING.md` § "GAP_MARKER
(level deletion)":

- **aggTrade stream**: `GAP_MARKER` is encoded as a TradePublic event with `qty == 0` AND
  `delta_id > 0` AND `delta_ts == 0`. Decoder recognises the triple-condition and skips.
- **depth@0ms stream**: per-level `qty == 0` is a level delete (upstream Binance). An
  **event-level** GAP_MARKER is encoded by writing a depth event with `bid_count == 0 &&
  ask_count == 0 && delta_updateId == 0` — impossible under normal operation, so unambiguous.
- **bookTicker stream**: `GAP_MARKER` = event with `delta_updateId == 0 && delta_ts == 0 &&
  bidQty == 0 && askQty == 0`.
- **snapshot stream**: snapshots are full state; a missed poll is encoded as a `CODER_RESET`
  block with `event_count = 0`.

Decoder sees a `GAP_MARKER` event and increments a "missed events" counter in its diagnostics;
it does **not** reconstruct the missing events.

---

## CODER_RESET block flag

See `FILE_FORMAT.md` § "CODER_RESET and Error Recovery". Summary:

- Emitted automatically every `kCoderResetEveryBlocks = 1024` blocks.
- Emitted on-demand by the writer whenever:
  - WS reconnect detected (incidents 1, 2).
  - updateId gap detected (incident 3).
  - Producer dropped events (incident 4) AND this is the first block after the drop.
- `CODER_RESET` block re-initialises:
  - Delta encoder state (last_price / last_ts / last_id / book_state → zero / empty).
  - AC / rANS coder state (Low, Range, buffered bytes reset; frequency tables reset to uniform).
  - Context byte (`last_payload_byte = 0`).
- The **first event** after a `CODER_RESET` writes absolute values (see
  `DELTA_ENCODING.md` § "Block-start absolute values").

---

## Recovery procedure (read side)

`hft-recorder-bench --repair <file.cxrec>` does this:

```
open(file)
read FileHeader; validate magic + version + CRC; else Status::BadMagic/CrcMismatch.
loop:
    read BlockHeader (32 B) at current offset.
    if magic != kBlockMagic OR CRC of header fails:
        scan forward byte-by-byte for next kBlockMagic that also has
        flags & kFlagCoderReset set; reset offset there.
        increment blocks_skipped_crc_total.
        continue.
    read payload (header.payload_size bytes).
    if crc32c(payload) != header.crc32c:
        as above — skip to next CODER_RESET block.
    pass (header, payload) to BlockDecoder.
    advance offset.
if file ends mid-block (payload read short): log and stop at last fully-valid block.
```

This is **read-only**; `--repair` does not rewrite the file. Use
`hft-recorder-bench --rewrite <in> <out>` (future work) to compact out the corrupted regions.

---

## Watchdog thread responsibilities

The control thread (CPU 7) provides:

1. **Heartbeat** — every 1 s, verify each producer/writer has made progress
   (`events_captured_total` or `blocks_flushed_total` increased). If no progress for 30 s:
   log `error`, set `watchdog_stalled{thread=…} = 1` gauge.
2. **Signal handling** — install handler for SIGTERM / SIGINT that sets `std::atomic<bool>
   g_stop{true}`. All loops poll.
3. **Metrics push** — every 10 s, push all `hft_recorder_*` metrics to Pushgateway if
   `PROMETHEUS_PUSHGATEWAY_URL` is set.
4. **Final shutdown** — on `g_stop`, wait for each producer thread to join (up to 5 s each),
   then each writer (up to 10 s), then exit. Hard-kill (SIGKILL to self) after 30 s if any
   thread refuses to exit.

---

## Exit codes

| Code | Meaning | Recoverable? |
|---|---|---|
| 0 | normal shutdown (SIGTERM/SIGINT) | N/A |
| 2 | configuration error (`.env` missing, bad CLI arg, `initBuildDispatch` failure) | fix config, re-run |
| 3 | filesystem error (permission denied, path does not exist) | fix permissions |
| 4 | all producers failed to start (library dispatch lookup failure → check exchange enum) | check library version |
| 5 | unrecoverable runtime error (watchdog forced exit) | check logs, re-run |
| 127 | `libcxet_lib.so` missing (from loader) | `./run.sh build` or install CXETCPP |

---

## References

- `FILE_FORMAT.md` — `CODER_RESET`, block payload layout, bitstream padding.
- `DELTA_ENCODING.md` § "GAP_MARKER" — per-level delete semantics.
- `API_CONTRACTS.md` § `Status` enum — the error surface.
- `CONFIG_AND_CLI.md` — signal handling wiring, CPU_CONTROL env var.
- `LOGGING_AND_METRICS.md` — which metrics fire for each incident row.
