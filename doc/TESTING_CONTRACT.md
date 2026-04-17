# hft-recorder — Testing Contract

Defines exactly which tests must exist, what each must assert, and how `ctest` targets are
wired. A feature is not merged until its entry in this document has a matching passing test.

---

## Layout

```
apps/hft-recorder/tests/
├── unit/                 ← fast, deterministic, no I/O, no threads
├── integration/          ← crosses file / thread boundaries; may run ~30 s total
└── bench/                ← google-benchmark; ctest label `bench`, not default
```

CMake exposes three targets:

| Target | Runs |
|---|---|
| `hftrec_unit_tests` | everything in `tests/unit/` via GoogleTest + rapidcheck |
| `hftrec_integration_tests` | everything in `tests/integration/` via GoogleTest |
| `hftrec_benchmarks` | everything in `tests/bench/` via google-benchmark |

`ctest` default set: `unit` + `integration`. Benchmarks opt-in via `ctest -L bench`.

---

## Unit tests (`tests/unit/`)

All unit tests link `hftrec_core` (the internal static library). No external I/O, no threads,
no `sleep_for`. Each test file is ≤ 200 lines.

| File | Must assert |
|---|---|
| `test_zigzag.cpp` | `zigzagEncode` / `zigzagDecode` roundtrip on `{0, ±1, ±2^31, ±INT64_MAX, INT64_MIN}`; property-based: `decode(encode(x)) == x` for 10 000 random int64. |
| `test_varint.cpp` | 1-byte / 2-byte / 10-byte (max for uint64) boundary values round-trip; malformed input (`0xFF` × 11) returns `Status::InvalidArgument`; property-based: `decode(encode(u)) == u` for 10 000 random uint64. |
| `test_crc32c.cpp` | Known test vector: `crc32c("123456789") == 0xE3069283`. Incremental update (`crc32cUpdate`) matches one-shot. |
| `test_laplace_halving.cpp` | After incrementing `count0/count1` to 4096, halving reduces to `≤ 2049 = (4095+1)/2 + 1`. Distribution ratio preserved within 1%. |
| `test_delta_trade.cpp` | rapidcheck property: a random sequence of `TradePublic` events → `TradeDeltaEncoder::appendEvent` → `TradeDeltaDecoder::decodeEvent` returns the original sequence (bit-exact). |
| `test_delta_bookticker.cpp` | Same as above for `BookTickerData`. |
| `test_delta_orderbook.cpp` | Same for raw depth@0ms deltas. Plus: `GAP_MARKER` (single-level `qty == 0`) is decoded as delete. |
| `test_delta_snapshot.cpp` | Snapshot round-trip: build `OrderBookSnapshot` with 1000 levels, encode, decode, compare level-by-level. |
| `test_codec_varint.cpp` | Round-trip on the delta byte stream (given synthetic byte blob). Output size ≤ 1.01× input. |
| `test_codec_ac_bin16_ctx0.cpp` | Round-trip on deterministic byte input `"aaabbbccc" × 100`. Ratio ≤ 0.90 (proves entropy compression happens). |
| `test_codec_ac_bin16_ctx8.cpp` | Same pattern + CTX8 state sync: encoder `last_payload_byte` matches decoder after every byte. |
| `test_codec_ac_bin16_ctx12.cpp` | Same. |
| `test_codec_ac_bin32_ctx8.cpp` | Same. |
| `test_codec_range_ctx8.cpp` | Subbotin carry propagation: adversarial input of long `0xFF` runs forces carry; round-trip bit-exact. |
| `test_codec_rans_ctx8.cpp` | rANS state initialisation, scalar encode + scalar decode round-trip. AVX2 interleaved decode produces the same bytes as scalar decode for the same input. |
| `test_block_writer.cpp` | `BlockWriter` appends 5000 events, triggers ≥ 8 flushes (via event-count, wall-time, and size thresholds individually). Re-opens file, reads back, all events present in order. |
| `test_block_flush_policy.cpp` | Three sub-tests, each verifying **one** flush trigger alone: 512 events; 1 s elapsed; 256 KB payload — the other two thresholds are disabled by stub clock / counters. |
| `test_spsc_ring.cpp` | Producer + consumer (same thread, alternating operations — no real threads here) round-trip 10 000 elements; `tryPush` on full ring returns `false`; `tryPop` on empty returns `false`; capacity is `N-1` usable slots (standard SPSC). |
| `test_file_header.cpp` | `FileHeader` serialises to 64 bytes exactly. CRC field in bytes 60..63. Round-trip. Magic / version mismatches return correct `Status`. |
| `test_block_header.cpp` | `BlockHeader` serialises to 32 bytes exactly. All `flags` values encode / decode correctly. |
| `test_gap_marker.cpp` | Synthetic SPSC overflow: encoder emits `GAP_MARKER` on next block boundary. Decoder sees it and increments its diagnostic counter. |
| `test_coder_reset.cpp` | After every 1024 blocks, flag is set and state is re-initialised (Low / Range / `last_payload_byte` all zero). |

Coverage target for `src/core/`: **≥ 85%** lines, measured via `lcov --capture` after a run of
`ctest -R hftrec_unit_tests`. Gap from 85% is a mergeability blocker.

---

## Integration tests (`tests/integration/`)

These may launch threads and do real file I/O into `/tmp`. Each test cleans up its files.

| File | Scenario |
|---|---|
| `test_roundtrip_all.cpp` | **The 28-cell matrix.** For each (stream ∈ {AggTrade, BookTicker, DepthUpdate, DepthSnapshot}, codec ∈ 7 codecs): write a synthetic 10 000-event `.cxrec` with that codec; read it back; compare decoded events to input — must be bit-exact. 28 parametrised GoogleTest cases. |
| `test_block_corruption_recovery.cpp` | Write 50-block file; flip one bit in the payload of block #20; `hft-recorder-bench --repair` must recover blocks 0..19 and 21..49 (block #20 skipped, not crashed). |
| `test_coder_reset_recovery.cpp` | Write 2048-block file (→ 2 CODER_RESETs); truncate at byte N mid-way through block 15; open in read mode; verify readable blocks = 0..14, and that scan-forward from byte N finds the CODER_RESET at block 1024. |
| `test_gap_injection.cpp` | Use a stub producer that drops every 7th event. After capture, decoded stream has the expected `GAP_MARKER` events at those positions. |
| `test_updateid_gap.cpp` | Stub depth producer generates `updateId = 1, 2, 3, 100`. Decoder sees one event, one event, one event, `CODER_RESET + GAP_MARKER`, then continues. |
| `test_live_smoke.cpp` | Optional / skipped when `BINANCE_API_KEY` empty: 30-second real capture. Asserts: file size > 0; at least 100 events captured on each stream; all blocks pass CRC. Tagged `live`. |
| `test_signal_shutdown.cpp` | Spawn recorder as child process, let it capture 5 s, send SIGTERM; child must exit 0 within 3 s; resulting file must end on a completed block (no trailing partial). |
| `test_env_parser.cpp` | Bad `.env` contents produce `Status::InvalidArgument` with the correct error message. Valid overrides reach `WriterConfig`. |

Integration tests tagged `live` require real API keys and are **not** part of CI default.
Run via `ctest -L live`.

---

## Benchmarks (`tests/bench/`)

Not part of default `ctest`. Run via `ctest -L bench` or directly
`./build/hftrec_benchmarks --benchmark_filter=<regex>`.

| File | Benchmark |
|---|---|
| `bench_varint.cpp` | Encode / decode 1M synthetic uint64s. Report μs/op + MB/s. |
| `bench_codec_<name>.cpp` × 7 | Encode + decode a synthetic block of each stream type. Report per-block ns via RDTSC harness (same as `probes/LatencyTracker`). |
| `bench_delta_<stream>.cpp` × 4 | Delta encode + decode 1M events per stream. |
| `bench_block_writer.cpp` | End-to-end: synthetic events → BlockWriter → `pwrite` (to `/tmp`) → measured throughput. |
| `bench_spsc_ring.cpp` | Producer + consumer threads, measure items/sec at various capacities. |

Benchmark failure = regression ≥ 10% vs recorded baseline in `tests/bench/baselines.json`.

---

## Fixtures

`tests/fixtures/` contains small pre-captured `.cxrec` files (each ~100 KB) for deterministic
regression testing:

| File | Notes |
|---|---|
| `trades_btcusdt_10s.cxrec` | Real Binance fapi capture, 10 s, VARINT codec. |
| `bookticker_btcusdt_10s.cxrec` | Same. |
| `depth_btcusdt_10s.cxrec` | Same. |
| `snapshot_btcusdt_once.cxrec` | Single REST snapshot. |
| `corrupted_midblock.cxrec` | `trades_btcusdt_10s.cxrec` with byte N bit-flipped (used by `test_block_corruption_recovery`). |

Fixtures are binary; check them in. Changing a fixture requires a `README.md` note explaining
why and a version bump.

---

## CI wiring

`.github/workflows/hft-recorder.yml` (future):

```yaml
- build hft-recorder in WSL-like container
- ctest -R hftrec_unit_tests --output-on-failure
- ctest -R hftrec_integration_tests --output-on-failure
- lcov capture + fail if coverage < 85%
- ctest -L bench (best-effort; compare to baseline; warning only, not blocking)
```

Live tests (`-L live`) run nightly on a cron-scheduled runner with `BINANCE_API_KEY` injected.

---

## What "done" means for a codec

A codec implementation can be marked done only when **all** of these pass:

1. `test_codec_<name>.cpp` unit test green.
2. `test_roundtrip_all.cpp` parametrised case for this codec × every stream green.
3. `test_coder_reset_recovery.cpp` passes when this codec is selected.
4. `bench_codec_<name>.cpp` produces numbers within 10% of baseline (baseline may be empty
   initially; then this run establishes it).
5. `hft-recorder-bench --codec <name> --roundtrip <fixture>` returns exit 0 for each of the
   4 fixtures.

A delta encoder (per-stream) is done when:

1. `test_delta_<stream>.cpp` rapidcheck property green.
2. `test_gap_marker.cpp` for this stream green.
3. Integration roundtrip green for every codec paired with this delta.

---

## References

- `API_CONTRACTS.md` — interfaces under test.
- `FILE_FORMAT.md` — header/block layouts asserted in `test_file_header` / `test_block_header`.
- `ARITHMETIC_CODING.md` — Subbotin carry + rANS algorithms tested in `test_codec_*`.
- `DELTA_ENCODING.md` — GAP_MARKER + CODER_RESET semantics tested in integration.
- `ERROR_HANDLING_AND_GAPS.md` — incidents simulated in integration tests.
