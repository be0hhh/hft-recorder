# hft-recorder

[![corpus-contract](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml/badge.svg?branch=main)](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml?query=branch%3Amain)

hft-recorder is the GUI-first capture, corpus, validation and compression-lab
product in the CXET source family.

## Current product contract

- Qt 6 + QML desktop workflow;
- normalized JSON session corpus for direct Recorder capture;
- sealed sharded binary corpus for Parser-wide capture;
- replay, validation and charts over canonical corpus data;
- baseline and custom compression experiments over the same corpus;
- active WSL recordings under "/mnt/d/recordings" when that directory exists.

Recorder owns captured corpus files. It does not own exchange endpoint/wire
grammar, Trader execution, Backtest fills or Parser public topology.

## Source and build boundary

The CXET root is the only canonical family build graph. Recorder compiles as a
direct source target and consumes the already-defined public "cxet::cxet_lib",
Parser producer-client, compressor and corpus-contract targets.

Recorder does not:

- compile CXET implementation sources;
- include CXET "network/", "parse/" or "exchanges/" internals;
- copy or stage a sibling SDK;
- import a sibling shared library as a family fallback;
- download a missing family dependency.

A standalone Recorder configure may fail with a clear message when the family
graph is absent.

## Capture modes

### Direct GUI capture

The first direct capture milestone records normalized Binance FAPI streams into
one user-selected session directory:

- Trades;
- BookTicker;
- Orderbook, seeded by an initial snapshot and followed by bounded deltas.

The session JSON files are the canonical input for validation, charts and the
compression lab.

### Parser-wide capture

Recorder attaches to Parser's same-UID capture contract. It does not launch,
replace or configure parserd. A capture has explicit duration and byte limits;
the first limit reached stops admission, drains committed records within the
reserved final budget, imports loss evidence and seals the corpus.

Backtest selection over the sealed corpus is explicit by source, receive-time
range and strategy-required channels. Missing, stale, degraded, gapped or
generation-crossing input fails closed.

## User workflow

1. Choose an output directory, normally below "/mnt/d/recordings".
2. Select exchange, market, symbols, logical streams and limits.
3. Capture a session.
4. Validate the corpus and inspect charts.
5. Run baseline and custom compression pipelines.
6. Compare ratio, encode/decode speed and lossless verification per stream
   family.

## Repository map

- "src/core/capture/" — capture lifecycle and normalized sinks;
- "src/core/corpus/" — canonical corpus readers/writers;
- "src/core/validation/" — integrity and semantic validation;
- "src/core/lab/" — compression experiments and reports;
- "src/gui/" — Qt/QML product;
- "corpus-contract/" — directly compiled shared corpus contract;
- "doc/" — product and research documentation.

Start with [doc/README.md](doc/README.md).

## CI coverage

The `corpus-contract` workflow overlays the triggering Recorder revision into
the private CXETCPP root graph, builds the exact
`hft-backtest-session-loader-tests` consumer target and runs only that CTest.
This checks the Recorder-owned corpus contract through its Backtest consumer.
It does not configure, build or test the full Recorder Qt/QML GUI product.

The root checkout requires the repository secret `CI_CXETCPP_SSH_KEY`. Fork
pull requests without that secret fail explicitly before checkout and do not
provide a passing corpus-contract result.
