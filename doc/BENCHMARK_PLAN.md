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
