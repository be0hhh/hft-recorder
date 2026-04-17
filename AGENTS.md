# Agent rules - hft-recorder (local nested repo)

This repository is a standalone application over `CXETCPP`.

## Core contract

- `hft-recorder` is not part of the core `CXETCPP` library.
- It consumes `CXETCPP` as a prebuilt dependency:
  - shared library: for example `../build/libcxet_lib.so`
  - public headers: only the API surface required by the app
- Do not compile `CXETCPP` sources inside this repo.
- Do not use `add_subdirectory(..)` or vendor the parent library source tree here.
- Do not depend on `network/`, `parse/`, `exchanges/`, or other library internals.

## Current truth

- Main product direction: `Qt 6 + QML` GUI-first application
- First milestone: capture normalized market data into canonical JSON corpus,
  replay it, validate it, and visualize it
- Compression research happens on top of that corpus
- The canonical corpus is more important right now than the old `.cxrec`-first plan

## Current work priorities

1. `src/core/capture/`
2. `src/core/corpus/`
3. `src/core/validation/`
4. `src/gui/`
5. `src/core/lab/`
6. `src/variants/`

## Stream semantics

- Never assume `trades == aggTrade`.
- The current Binance FAPI library path is semantically tied to `aggTrade`.
- Keep that distinction explicit in filenames, schema, and benchmark labels.

## Research priorities

- Baseline comparisons are mandatory:
  - `zstd`
  - `lz4`
  - `brotli`
  - `xz/lzma`
- Custom ideas are mandatory:
  - trade-specific
  - L1-specific
  - orderbook-specific
- Rankings must be per stream family, not global.

## GUI product rule

- The GUI is not a thin wrapper around CLI tools.
- The GUI is part of the deliverable:
  - capture
  - session browsing
  - validation
  - compression lab
  - results dashboard
