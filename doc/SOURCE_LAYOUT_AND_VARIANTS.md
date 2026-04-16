# hft-recorder - source layout and variants

## Purpose

This document fixes the concrete source layout so implementation does not drift.

It also defines how to name and organize the many candidate C++ variants.

## Top-level code layout

```text
include/hftrec/
src/app/
src/core/
src/variants/
src/support/
tests/
bench/
scripts/python_lab/
```

## `src/app/`

Application layer only.

Planned files:
- `main.cpp`
- `capture_cli.cpp`
- `analyze_cli.cpp`
- `benchmark_cli.cpp`
- `report_export.cpp`

This layer should not own heavy compression logic.

## `src/core/`

Stable shared implementation.

Recommended subdirectories:
- `common/`
- `dataset/`
- `stream/`
- `representation/`
- `codec/`
- `block/`
- `metrics/`
- `online/`

Examples of core responsibilities:
- common block writer
- common metadata structs
- common benchmark registration
- wrappers for standard codecs
- shared varint / bit-packing helpers

## `src/variants/`

This is the experimental C++ variant forest.

Layout:

```text
src/variants/
  trade/
  l1/
  orderbook/
```

Inside each family, every candidate gets its own directory:

```text
src/variants/trade/var01_raw_zstd_cpp/
src/variants/trade/var02_delta_varint_cpp/
src/variants/trade/var03_delta_zstd_cpp/
...
```

The same pattern applies to `l1/` and `orderbook/`.

## Naming convention

Use:
- `varNN_..._cpp` for C++ candidates
- `varNN_..._pyproto` for Python-side mirrored prototypes in docs/reports

Examples:
- `var01_raw_zstd_cpp`
- `var02_delta_varint_cpp`
- `var03_columnar_zstd_cpp`
- `var04_spread_anchor_cpp`
- `var05_keyframe_delta_cpp`

Keep the number stable once assigned so benchmark tables remain consistent.

## Mandatory files per variant

Every variant directory should contain:
- `README.md`
- `encode.hpp`
- `encode.cpp`
- `decode.hpp`
- `decode.cpp`
- `metadata.hpp`

Optional:
- `bench_case.cpp`
- `notes.md`

## First concrete variant set

### Trade
- `var01_raw_zstd_cpp`
- `var02_delta_varint_cpp`
- `var03_delta_zstd_cpp`
- `var04_columnar_zstd_cpp`
- `var05_custom_trade_pack_cpp`

### L1
- `var01_raw_lz4_cpp`
- `var02_bidask_delta_cpp`
- `var03_spread_anchor_cpp`
- `var04_change_mask_cpp`
- `var05_custom_l1_pack_cpp`

### Orderbook
- `var01_raw_updates_cpp`
- `var02_changed_levels_cpp`
- `var03_topn_cpp`
- `var04_keyframe_delta_cpp`
- `var05_anchor_relative_cpp`
- `var06_reconstruction_first_cpp`

## Why this layout

This structure gives:
- high isolation between ideas
- easy benchmark registration
- easy rejection of weak variants
- clear per-stream specialization
- a path to later promote common pieces into `src/core/`

## Promotion rule

Do not move code from `src/variants/` into `src/core/` too early.

Promote only when:
- more than one variant needs the same helper
- the helper is clearly stable
- the shared abstraction does not harm the hot path
