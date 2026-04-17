# hft-recorder - overview

## What this project is

`hft-recorder` is a GUI-first market-data capture and compression research
platform on top of `CXETCPP`.

It is no longer framed as:
- only a CLI recorder
- only a `.cxrec` file-format project
- only a benchmark harness

The actual product now has two equally important halves:
- a capture / replay / visualization desktop app
- a compression lab for baseline and custom algorithms

## The first real milestone

The first fully defensible milestone is:
- capture normalized Binance FAPI market data
- store it in a clean canonical JSON corpus
- open that corpus in a Qt GUI
- validate and visualize the data
- run compression experiments on the same session
- compare ratio, speed, and exactness

Without that, compression results are too detached from the real dataset and too
weak for coursework and future backtest use.

## Canonical session model

Each recorded session is stored as:

```text
recordings/<session_id>/
  manifest.json
  snapshot_000.json
  snapshot_001.json
  ...
  depth.jsonl
  trades.jsonl
  bookticker.jsonl
  derived/
  reports/
```

The canonical corpus is the truth for:
- replay
- validation
- benchmark inputs
- GUI charts
- future backtest adapters

## What "clean data" means

“Clean data” in this project does not mean raw exchange JSON.

It means:
- data received through public `CXETCPP` entrypoints
- parsed and normalized into `CXETCPP` structs
- serialized by `hft-recorder` into its own JSON schema

Examples:
- trade-like events are saved as normalized trade records
- book ticker events are saved as normalized L1 records
- orderbook delta events are saved as normalized changed-level events
- snapshots are saved as normalized full-book snapshots

## Compression research goal

The coursework still aims to produce custom compression ideas, not only to wrap
standard libraries.

Required comparisons:
- `zstd`
- `lz4`
- `brotli`
- `xz/lzma`

Required custom families:
- trade-specific
- L1 / bookTicker-specific
- orderbook-specific

All serious winner tables are measured against the canonical JSON corpus or a
canonical normalized binary derivation of it.

## GUI-first product philosophy

The user experience must be direct:
1. capture a session
2. inspect the session visually
3. run benchmarks
4. see rankings and charts
5. export coursework-friendly results

That means the GUI is not just a shell around CLI tools. It is part of the
deliverable.
