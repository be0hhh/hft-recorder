# Agent rules - hft-recorder (local nested repo)

This repository is a standalone application over `CXETCPP`.

## Core contract

- `hft-recorder` is not part of the core `CXETCPP` library.
- It consumes `CXETCPP` as a prebuilt dependency:
  - shared library: for example `../build/libcxet_lib.so`
  - public headers: only the API surface required by the recorder
- Do not compile `CXETCPP` sources inside this repo.
- Do not use `add_subdirectory(..)` or vendor the parent library source tree here.
- Do not depend on `network/`, `parse/`, `exchanges/`, or other library internals.

## Current task focus

- The current work area is this repo plus `doc/`.
- Main goal: build a custom, highly specialized market-data compression library.
- The benchmark and recorder parts exist to prove which custom ideas actually win.
- The repo should be treated as `custom-library-first`, not `generic benchmark lab first`.

## Research priorities

- Generate many custom ideas, not just one or two obvious transforms.
- Cover all three first-class stream families:
  - `trades / aggTrade`
  - `bookTicker` as `L1`
  - `orderbook updates`
- Compare against standard baselines:
  - `zstd`
  - `lz4`
  - `gzip` / `zlib`
  - `brotli`
  - `lzma/xz`
- Use Python as a research laboratory.
- Promote winning ideas into the C++ core.
- Allow different winners for different stream families and different modes.

## Practical integration notes

- Treat `CXETCPP` as "good enough for recorder work" unless the public API directly blocks implementation.
- `CxetStream<T>` is the main expected starting point for recorder integration.
- Current practical stream types are strongest for:
  - `TradePublic`
  - `BookTickerData`
  - `MarkPriceData`
- `OrderBookSnapshot` is heavy; do not assume it behaves like a tiny queue event.
- Online compression during recording is a hard requirement for the live-path winner.

## Trade feed semantics

- Never assume `trades == aggTrade`.
- The current Binance FAPI library path is semantically tied to `aggTrade`.
- Binance Spot exposes raw `@trade`.
- Binance USDⓈ-M Futures documentation checked so far confirms `@aggTrade`; do not model raw futures trades as already available unless verified again.
- File format metadata and benchmark naming must preserve the distinction between:
  - aggregated trades
  - raw trades

## Current network truth relevant to recorder design

- Current transport baseline in `CXETCPP`:
  - REST = `Boost.Beast`
  - WebSocket = `Boost.Beast`
  - JSON = `simdjson`
  - TLS/crypto baseline = `OpenSSL`
- Current local market-data pipeline expectations are in nanoseconds / low microseconds.
- Exchange/network path remains milliseconds.
- For recorder design, do not confuse current implementation truth with future roadmap items such as:
  - `liburing`
  - `wolfSSL`
  - custom shared WS runtime
