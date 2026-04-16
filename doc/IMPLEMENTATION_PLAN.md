# hft-recorder - implementation plan

## Goal

Build `hft-recorder` as a **specialized market-data compression system** with:
- a C++ core library
- a Python research laboratory
- a recorder / benchmark harness

The implementation must optimize for both:
- compression strength
- runtime cost on the live path

The project should not be implemented as "one codec and done".
It should be implemented as a framework for many stream-specialized candidates, with a path to promote winners into the core.

## Final architectural model

The implementation should converge to four code zones.

### 1. `src/core/`

Permanent C++ library code that is expected to survive long term.

Responsibilities:
- common stream-family abstractions
- block format helpers
- common transform primitives
- common codec wrappers
- metrics and benchmark helpers
- online recorder plumbing

This is the stable center.

### 2. `src/variants/`

C++ implementations of candidate pipelines.

Each variant is isolated enough to benchmark and compare independently.
This directory is intentionally large and experimental.

### 3. `src/app/`

Application entrypoints and orchestration.

Responsibilities:
- live capture
- offline re-run over stored datasets
- benchmark execution
- result export

### 4. `scripts/python_lab/`

Python-side research environment.

Responsibilities:
- offline dataset transforms
- baseline compression comparison
- quick idea prototypes
- result analysis and plotting

Winning ideas from here are later ported into `src/variants/` or `src/core/`.

## Source tree layout

Recommended layout:

```text
apps/hft-recorder/
  include/
    hftrec/
      core/
      variants/
      api/
  src/
    app/
      main.cpp
      capture_cli.cpp
      analyze_cli.cpp
      benchmark_cli.cpp
    core/
      common/
      dataset/
      stream/
      representation/
      codec/
      block/
      metrics/
      online/
    variants/
      trade/
        var01_raw_zstd_cpp/
        var02_delta_varint_cpp/
        var03_delta_zstd_cpp/
        var04_columnar_zstd_cpp/
        var05_custom_trade_pack_cpp/
      l1/
        var01_raw_lz4_cpp/
        var02_bidask_delta_cpp/
        var03_spread_anchor_cpp/
        var04_change_mask_cpp/
        var05_custom_l1_pack_cpp/
      orderbook/
        var01_raw_updates_cpp/
        var02_changed_levels_cpp/
        var03_topn_cpp/
        var04_keyframe_delta_cpp/
        var05_anchor_relative_cpp/
        var06_reconstruction_first_cpp/
    support/
      external_wrappers/
  tests/
    core/
    variants/
    roundtrip/
  bench/
    micro/
    datasets/
    reports/
  scripts/
    python_lab/
      baselines/
      transforms/
      orderbook/
      notebooks/
      reports/
```

## Variant design rule

Each variant directory should represent a **full candidate pipeline**, not only one tiny helper.

A variant should answer:
- which stream family it targets
- which representation it uses
- which transform path it uses
- which codec it uses
- whether it is aimed at online, archive, or replay mode

Minimum files inside a variant:
- `README.md`
- `encode.hpp/.cpp`
- `decode.hpp/.cpp`
- `metadata.hpp`
- `bench_case.cpp` or equivalent hookup

## Stream-family implementation strategy

### Trades / aggTrade

Implement first:
- raw binary baseline
- delta + varint
- delta + zstd
- columnar + zstd
- one custom packed trade variant

Specialization focus:
- id delta
- timestamp delta
- price delta
- qty delta
- side packing
- block-local anchors

### L1 / bookTicker

Implement first:
- raw baseline
- bid/ask delta
- spread-relative variant
- change-mask variant
- one custom pair-packed L1 variant

Specialization focus:
- bid/ask symmetry
- spread handling
- very cheap live encode path
- compact field mask paths

### Orderbook

Implement first:
- raw updates baseline
- changed-level list representation
- top-N projection representation
- keyframe + delta representation
- anchor-relative representation

Specialization focus:
- representation before codec
- reconstruction complexity
- gap behavior
- bytes per changed level

Orderbook should not be blocked on trades/L1 being "done".
It should have its own early implementation branch.

## Core interfaces to define early

These interfaces should be documented and then kept stable enough for many variants.

### `StreamFamily`

Values:
- `trade_like`
- `l1_bookticker`
- `orderbook_updates`

### `PipelineProfile`

Values:
- `online_recording`
- `archive`
- `replay`

### `RepresentationStrategy`

Represents the layout before entropy coding.

Examples:
- `raw_row`
- `delta_row`
- `columnar`
- `changed_levels`
- `topn`
- `keyframe_delta`
- `anchor_relative`

### `CodecStrategy`

Represents the final compressor.

Examples:
- `none_raw`
- `varint`
- `zstd`
- `lz4`
- `gzip`
- `brotli`
- `lzma`
- `custom_*`

### `VariantId`

A stable label used in reports and benchmarks.

Recommended format:
- `trade.var02.delta_varint_cpp`
- `l1.var04.change_mask_cpp`
- `orderbook.var05.anchor_relative_cpp`

## How `extra/` should be used

The implementation is allowed to use vendored dependencies from parent `extra/` if they help the system materially.

Recommended usage:
- `fmtlib` for formatting and report output
- `spdlog` for structured logging
- `google-benchmark` for micro and pipeline benchmarks
- `HdrHistogram_c` for latency distributions
- `moodycamel` for online queue experiments if needed
- `rapidcheck` for roundtrip and invariants
- `pybind11` only if later needed to expose selected C++ primitives to Python

Do not pull everything into the hot path by default.
Dependencies should be chosen by purpose:
- benchmarking
- testing
- logging
- wrappers around standard codecs

## Implementation phases

### Phase 1. Core scaffolding

Create:
- source tree layout
- common enums and variant metadata
- dataset naming and metadata structs
- result table schema
- benchmark registration model

Output:
- buildable skeleton without real compression logic beyond placeholders

### Phase 2. Baseline pipelines

Implement in both Python and C++:
- raw + `zstd`
- raw + `lz4`
- delta + `varint`
- delta + `zstd`

For:
- trade-like
- L1

Orderbook:
- raw updates baseline
- changed-level representation baseline

Output:
- first honest baseline matrix

### Phase 3. Custom family prototypes

Implement first wave of custom variants:
- one trade-packed family
- one L1-packed family
- two orderbook representation families

Output:
- first meaningful custom-vs-baseline comparison

### Phase 4. Online path

Add live-mode execution path:
- receive data from `CXETCPP`
- transform immediately
- compress by rolling block
- write compressed output directly

Output:
- sustained live ingest measurements

### Phase 5. Promotion and cleanup

Promote the strongest winners from `src/variants/` into reusable `src/core/` abstractions where justified.

Do not prematurely generalize weak ideas.

## Benchmark reporting contract

Every implementation phase should update the same style of report:
- per-stream winners
- online winner
- archive winner
- replay winner
- rejected ideas and why they lost

This is mandatory to keep the coursework defensible.

## Architecture rules

- Favor stream-family specialization over fake generic purity.
- Avoid one giant inheritance-heavy codec framework.
- Keep hot-path encode code simple and data-oriented.
- Separate representation from final codec choice.
- Do not assume the same best answer for trades, L1, and orderbook.
