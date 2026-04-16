# hft-recorder - overview

## What this project is

`hft-recorder` is no longer framed as "just a recorder app".

The central goal is to build a **second, highly specialized compression library**
for market data on top of `CXETCPP`.

This coursework is about:
- inventing many custom compression ideas
- tailoring them separately for:
  - `trades / aggTrade`
  - `L1 bookTicker`
  - `orderbook updates`
- comparing them honestly against standard codecs
- proving which approaches are actually best on real exchange data

So the real deliverable is not only a benchmark table.
The real deliverable is a **custom C++ compression core** plus the evidence that justifies it.

## Why standard codecs are still required

Standard compressors are not the center of the project, but they are mandatory.

They serve as:
- baseline
- proof layer
- reality check against overengineering

Every custom pipeline must be compared against at least:
- `zstd`
- `lz4`
- `gzip` / `zlib`
- `brotli`
- `lzma/xz`

If a custom idea does not beat a strong baseline in a meaningful profile, it is not a winner.

## Main research question

For 20-minute live datasets of:
- `trades / aggTrade`
- `bookTicker` as `L1 orderbook`
- `orderbook updates`

what is the best pipeline for each stream family, where pipeline means:
- representation strategy
- transform strategy
- codec strategy
- operating mode (`online recording`, `archive`, `replay`)

There is no requirement that one universal algorithm must win.
Different streams are allowed to have different winners.

## Project philosophy

This project must be **custom-library-first**.

That means:
- many custom ideas are expected
- Python is allowed and encouraged for fast experiments
- but the final core library is expected to be C++
- the benchmark system exists to prove and rank the custom ideas

## Three equal domains

### 1. Trades / aggTrade

This is the first market-event family.

Main research focus:
- timestamp/id/price/qty delta models
- compact event packing
- side-pattern exploitation
- micro-batching
- dictionary and block-local structure

### 2. L1 / bookTicker

Treat `bookTicker` explicitly as `level 1 orderbook`.

Main research focus:
- bid/ask relative encoding
- spread-aware transforms
- change-mask encoding
- symmetric price/qty packing
- extremely cheap online compression

### 3. Orderbook updates

This is the hardest and most important family.

The main challenge is not only compression.
It is choosing the right representation before compression:
- raw update
- changed-level list
- top-N projection
- keyframe + delta chain
- anchor-relative price encoding
- reconstruction-oriented layouts

## Output of this phase

This phase must produce:
- a custom idea catalog
- real captured corpora
- a reproducible comparison matrix
- per-stream winners
- one recommended online path
- one recommended archive path
- one recommended replay path

If the winners differ by stream family, that is acceptable and expected.

## Reading order

1. `RESEARCH_PROGRAM.md`
2. `CUSTOM_IDEA_CATALOG.md`
3. `COMPARISON_MATRIX.md`
4. `DATASET_CAPTURE_PROTOCOL.md`
5. `ORDERBOOK_REPRESENTATION_EXPERIMENTS.md`
6. `BENCHMARK_PLAN.md`
7. `CODEC_VARIANTS.md`
8. `STREAMS.md`
9. `ARCHITECTURE.md`
10. `IMPLEMENTATION_NOTES.md`
