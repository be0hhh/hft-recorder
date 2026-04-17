# hft-recorder - benchmark plan

## Goal

Build the proof framework for a specialized market-data compression library.

The benchmark must answer:
- which custom ideas are strong enough to promote into the C++ core
- what compresses best
- what decodes fastest
- what is cheap enough to run online during recording
- what representation works best for orderbook data

This is not a synthetic micro-benchmark only.
It must be driven by real captured datasets.

## Primary dataset plan

Use live Binance ETHUSDT futures data as the main corpus.

Minimum capture windows:
- `20 minutes` of trade-like stream
- `20 minutes` of `bookTicker`
- `20 minutes` of orderbook update stream

Prefer separate captures for:
- normal market period
- volatile market period

If possible, repeat the same methodology later for BTCUSDT as a cross-check.

## Streams to benchmark

At minimum:
- trade-like stream currently available from `CXETCPP`
- `bookTicker` as `L1`
- orderbook updates

If library semantics are still tied to `aggTrade`, label that dataset honestly as `aggTrade`.
Do not call it raw `trade` unless verified.

## Evaluation axes

For every method, record:
- `input_bytes`
- `output_bytes`
- `compression_ratio`
- `space_saving_percent`
- `encode_throughput_mb_s`
- `decode_throughput_mb_s`
- `encode_latency_per_event`
- `decode_latency_per_event`
- `peak_memory`
- `online_feasible` (`yes/no`)
- `implementation_complexity` (`low/medium/high`)

## Families to compare

### A. Standard codecs on raw binary

Run standard codecs on:
- raw row-oriented binary events
- raw captured blocks

Candidates:
- `zstd`
- `lz4`
- `gzip` / `zlib`
- `brotli`
- `lzma/xz`

### B. Standard codecs on transformed binary

Apply simple domain transforms first, then a standard codec:
- delta
- zigzag
- varint
- column split by field
- top-of-book relative encoding

Then compress with:
- `zstd`
- `lz4`
- `gzip`
- `brotli`

### C. Python reference path

Use Python for fast exploration on the same datasets.

Python benchmarks should validate:
- whether an idea is promising at all
- whether a transform is representation-good even if Python is too slow for live use

Python stack may include:
- `zstandard`
- `lz4`
- `gzip`
- `bz2`
- `lzma`
- `brotli`
- NumPy / pandas only if they help analysis, not as a requirement

### D. Custom and hybrid methods

At minimum compare:
- `delta + varint`
- `delta + zstd`
- `delta + lz4`
- `columnar + zstd`
- `columnar + delta + zstd`
- orderbook-specific transforms

Optional advanced experiments:
- arithmetic coding
- range coding
- rANS

These are optional until simpler baselines are measured.

## Benchmark matrix

Every tested method should be evaluated on each stream family:

| Stream | Raw generic | Transformed generic | Python prototype | Custom/hybrid |
|--------|-------------|---------------------|------------------|---------------|
| trade / aggTrade | yes | yes | yes | yes |
| bookTicker | yes | yes | yes | yes |
| orderbook updates | yes | yes | yes | yes |

## Orderbook-specific benchmark tasks

Orderbook must be benchmarked in more than one representation.

Compare at least:
- raw event payload as received
- compact list of changed levels only
- price delta relative to previous event
- price delta relative to current mid
- periodic snapshot + delta chain
- top-N only (`N = 10`, `25`, maybe `50`)

Metrics for orderbook must include:
- bytes per update
- bytes per changed level
- recovery complexity after gap
- decode cost of reconstructing the current book state

## Online vs offline benchmark split

There are two different benchmark modes.

### Offline analysis mode

Purpose:
- compare many methods quickly
- allow Python prototypes
- allow two-pass transforms

Flow:
- capture dataset once
- replay the same dataset through many compression pipelines

### Online recording mode

Purpose:
- measure whether the method can be used directly while downloading live data

Flow:
- event arrives from `CXETCPP`
- transform immediately
- compress immediately or by small rolling block
- write compressed block without mandatory raw dump first

Online mode must record:
- sustained events/sec
- backpressure behavior
- block flush latency
- dropped-event risk

## Decision rules

Use multiple winners, not one:

1. `best_live_codec`
   - best method that comfortably keeps up with live ingest

2. `best_archive_codec`
   - best ratio, even if encode is heavier

3. `best_replay_codec`
   - best decode speed with acceptable ratio

For orderbook, also choose:

4. `best_orderbook_representation`
   - best representation before compression

Also always summarize:

5. `best_trade_pipeline`
   - best full pipeline for trade-like data

6. `best_l1_pipeline`
   - best full pipeline for `bookTicker` / `L1`

7. `best_orderbook_pipeline`
   - best full pipeline for orderbook updates

## Deliverables

This benchmark phase should produce:
- result tables per stream
- a summary table across all codecs
- plots or CSV/JSON outputs
- a written recommendation
- one selected online pipeline for the first real recorder implementation

## Minimal first milestone

Before advanced custom codecs, complete this smaller matrix:
- raw binary + `zstd`
- raw binary + `lz4`
- delta + `zstd`
- delta + `lz4`
- delta + `varint`
- one Python prototype

Do this first for:
- trade-like stream
- `bookTicker`

Then expand to orderbook.

---

## Custom-codec implementation grid (7 × 4)

The seven concrete codec variants defined in
[CODEC_VARIANTS.md](CODEC_VARIANTS.md) are benchmarked against all four MVP
stream families. That gives a **7 × 4 = 28-cell** measurement grid.

### Measurement unit: one block

The offline bench tool (`hft-recorder-bench`) consumes a captured `.cxrec`
file (VARINT baseline), decodes each block to recover the raw event stream,
and then re-encodes each block through every codec. Metrics are recorded
**per block**, not per file — this gives per-block p50/p95/p99 distributions
rather than a single summary number.

Per-block metrics:

| Metric | Unit | Source |
|--------|------|--------|
| `encode_ns` | nanoseconds | `RDTSC` wrapped around the encode call, converted via `TscCalibration` |
| `decode_ns` | nanoseconds | same, around decode |
| `bytes_in` | bytes | block's uncompressed delta-encoded payload size |
| `bytes_out` | bytes | `compressed_size` from block header |
| `ratio` | — | `bytes_in / bytes_out` |
| `encode_mb_s` | MB/s | `bytes_in / (encode_ns * 1e-9) / 2^20` |
| `decode_mb_s` | MB/s | `bytes_in / (decode_ns * 1e-9) / 2^20` |
| `roundtrip_ok` | bool | `decoded == original` byte-for-byte — gate on every block |

### Metric export

Each bench run pushes a gauge vector to a Prometheus Pushgateway:

```
hft_recorder_bench_ratio{codec, stream, quantile}          = double
hft_recorder_bench_encode_mb_s{codec, stream, quantile}    = double
hft_recorder_bench_decode_mb_s{codec, stream, quantile}    = double
hft_recorder_bench_peak_bytes{codec, stream}               = uint64
```

`quantile` ∈ `{p50, p95, p99}`. `codec` ∈ the 7 labels. `stream` ∈ `{aggTrade,
depth0ms, bookTicker, snapshot}`.

A Grafana dashboard (shipped alongside the app in `grafana/dashboards/`)
reads these gauges directly and renders:

- ratio bar chart per stream, grouped by codec (tallest bar = best ratio)
- decode MB/s bar chart per stream (tallest = best replay speed)
- encode MB/s bar chart per stream (tallest = most live-capable)
- a summary table that highlights the best codec per stream per profile
  (archive / live / replay) — the "per-stream winners" required by
  `RESEARCH_PROGRAM.md`.

### Grid execution order

Run the grid in this order — stop and fix if any earlier cell fails
roundtrip:

1. VARINT × {aggTrade, depth0ms, bookTicker, snapshot} (sanity — must match source byte-for-byte)
2. AC_BIN16_CTX0 × all streams (AC layer validation)
3. AC_BIN16_CTX8 × all streams (context layer validation)
4. AC_BIN16_CTX12 × all streams
5. AC_BIN32_CTX8 × all streams (precision upgrade)
6. RANGE_CTX8 × all streams (Subbotin carry validation)
7. RANS_CTX8 × all streams (rebuild + SIMD decode validation)
