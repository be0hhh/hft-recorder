# hft-recorder - codec and transform variants

## Purpose of this document

This document is the codec-side experiment catalog for the future custom compression library.
It is not a claim that standard codecs are enough, and it is not a claim that one custom codec already won.
Its job is to organize codec candidates that compete inside the proof framework.

## Variant groups

### Group 1. Standard generic codecs

These are mandatory baselines.

Candidates:
- `zstd`
- `lz4`
- `gzip` / `zlib`
- `brotli`
- `lzma/xz`

Why they matter:
- they are battle-tested
- they are easy to integrate
- they may already be good enough on transformed binary
- any custom codec must beat them honestly

## Group 2. Simple domain transforms

These are not full codecs by themselves.
They are preprocessing stages applied before a generic codec or before raw varint output.

Candidates:
- delta encoding
- zigzag for signed deltas
- varint
- bit-packing
- field-wise column split
- timestamp normalization
- price-relative encoding

Important point:
- a good transform + standard codec may beat a complex custom entropy coder

## Group 3. Hybrid pipelines

Examples:
- raw binary -> `zstd`
- raw binary -> `lz4`
- delta -> `zstd`
- delta -> `lz4`
- delta + varint -> `zstd`
- columnar -> `zstd`
- columnar + delta -> `zstd`
- columnar + delta -> `lz4`

These hybrid pipelines are likely the first serious contenders for the live path.

## Group 4. Python prototypes

Python variants are reference experiments.
They are allowed even if they are too slow for production recording.

Purpose:
- validate representation ideas quickly
- try many transforms before writing C++
- estimate whether a custom idea is worth porting

Examples:
- Python delta + `zstandard`
- Python field-columnar packing + `zstandard`
- Python orderbook changed-level packing + `lz4`
- Python arithmetic coding prototype for ratio-only comparison

## Group 5. Custom entropy coders

These are advanced candidates and a major part of the coursework ambition.

Possible directions:
- arithmetic coding
- range coding
- rANS

Use them when they clearly improve one of the target profiles:
- live recording
- archive ratio
- replay speed

If simpler baselines are already strong enough for a specific stream family, that result is still valid.
But the coursework should actively search for better custom alternatives before concluding that the baseline wins.

## Evaluation profiles

Each variant should be judged against one of three profiles.

### Live profile

Needs:
- stable online encode
- low CPU overhead
- low write latency
- simple block streaming

Likely candidates:
- `lz4`
- `zstd` low levels
- delta + `lz4`
- delta + `zstd`
- delta + varint

### Archive profile

Needs:
- strongest ratio
- slower encode is acceptable
- decode speed is secondary

Likely candidates:
- `zstd` medium levels
- `brotli`
- `lzma`
- custom entropy coding on top of strong transforms

### Replay profile

Needs:
- very fast decode
- still reasonable ratio

Likely candidates:
- `lz4`
- `zstd`
- rANS if it materially wins

## Recommended experimental order

Do not start from the hardest codecs.
Use this order:

1. raw binary + standard codecs
2. delta + standard codecs
3. field-aware transforms + standard codecs
4. Python prototypes for new ideas
5. custom entropy coders only where still justified

## Orderbook-specific variants

Orderbook should not only vary the compressor.
It must also vary the representation.

Representations to compare:
- raw updates
- changed-level only
- top-N only
- snapshot + delta chain
- price index relative to a moving anchor
- per-side separate columns

Then each representation can be compressed with:
- `zstd`
- `lz4`
- `gzip`
- custom path if needed

## Keep these assumptions out of the code

Until benchmark results exist, do not hard-code assumptions such as:
- "arithmetic coding is definitely best"
- "rANS is definitely required"
- "custom codec is always better than zstd"
- "one codec should be used for all streams"

The output of this research should probably be:
- one winner for live recording
- one winner for archival storage
- one winner for replay

and possibly different winners per stream type.

---

## Concrete custom-codec variants (implementation targets)

The research catalog above stays exploratory. For the first implementation
pass, these are the seven `codec_id` values enumerated in
[FILE_FORMAT.md](FILE_FORMAT.md). Every one of them is a candidate that must
be built and benchmarked; the eventual "winner" per stream emerges from the
bench matrix, not from this document.

| # | `codec_id` | Label | Description | Expected ratio (vs VARINT) | Expected decode | Best for (hypothesis) |
|---|-----------|-------|-------------|----------------------------|-----------------|----------------------|
| 1 | `0x01` | `VARINT` | Delta + ZigZag + LEB128 VarInt. No entropy coder. | 1.00× (baseline) | very fast (~2 GB/s) | Live recording; baseline for all bench comparisons |
| 2 | `0x02` | `AC_BIN16_CTX0` | Binary AC, 16-bit prob precision, single global context | ~1.3× | ~180 MB/s | Proof-of-concept AC layer; not expected to be a winner |
| 3 | `0x03` | `AC_BIN16_CTX8` | `AC_BIN16` + 8-bit context = low byte of last emitted payload byte (256 models) | ~1.7× | ~200 MB/s | Archive profile — `aggTrade`, `bookTicker` |
| 4 | `0x04` | `AC_BIN16_CTX12` | 12-bit context = field_id(4) \| bit_position(4) \| recent_byte_high(4) (4096 models, fits L1) | ~1.8× | ~180 MB/s | Archive profile — `orderbook_delta` (more field variety) |
| 5 | `0x05` | `AC_BIN32_CTX8` | Same as `AC_BIN16_CTX8` but 32-bit prob precision (needs `__uint128_t` multiply) | ~1.8× | ~130 MB/s | Ratio ceiling reference; not expected to beat CTX12 meaningfully |
| 6 | `0x06` | `RANGE_CTX8` | Subbotin range coder (64-bit Low / 32-bit Range), 8-bit context | ~1.7× | ~250 MB/s | Live profile — fast encode, reasonable ratio |
| 7 | `0x07` | `RANS_CTX8` | rANS order-1, 8-bit context, quasi-adaptive rebuild at every 512-event block. AVX2 4-way interleaved decode. | ~1.7× | ~700 MB/s (scalar) / ~1430 MB/s (AVX2) | Replay profile — archive stays in this format for fast backtest load |

See:
- [ARITHMETIC_CODING.md](ARITHMETIC_CODING.md) for the AC / range-coder theory and the Subbotin & rANS pseudocode.
- [DELTA_ENCODING.md](DELTA_ENCODING.md) for the per-field input the codecs receive.
- [BENCHMARK_PLAN.md](BENCHMARK_PLAN.md) for the 7 × 4 stream measurement grid that decides which hypothesis survives.

All seven codecs share the same delta-encoding front-end; only the entropy
stage differs. This keeps the implementation cost manageable and means a
single captured `.cxrec` file (in `VARINT` format) is enough to re-bench every
codec offline — no re-capture needed when tuning.
