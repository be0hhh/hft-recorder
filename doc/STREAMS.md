# hft-recorder - streams and representation questions

## Purpose

This document defines what data families `hft-recorder` studies and what representation problems must be solved before compression decisions are final.

## MVP scope

For the first end-to-end pass (record → bench → pick a codec), the recorder captures
**four** streams from Binance fapi:

| Stream | CXETCPP entrypoint | Event carrier | Notes |
|--------|-------------------|---------------|-------|
| `aggTrade` | `cxet::subscribe().object(trades)…build(buf)` → `CxetStream<TradePublic>` | `TradePublic` | Single-thread consume via the stream wrapper's SPSC ring. |
| `depth@0ms` | `runSubscribeOrderBookDeltaByConfig(cfg, OrderBookDeltaOnUpdate cb)` | `OrderBookSnapshot` (partial) | **Raw diff book updates**, no throttling — the recorder runs its own IO-bound thread that blocks inside the library's `run…` call and invokes the callback. Not available via `CxetStream<T>`. |
| `bookTicker` | `cxet::subscribe().object(bookticker)…build(buf)` → `CxetStream<BookTickerData>` | `BookTickerData` | Same pattern as aggTrade. |
| `orderbook_snapshot` | `runGetOrderBookByConfig(cfg, out)` | full book on demand | REST, periodic (60 s) via a dedicated poller thread. |

`fundingRate` / `markPrice` are **out of scope** for the MVP — the file format
keeps the `stream_type` slot reserved for future use, but the recorder emits
no bytes on that channel.

## Stream families

### 1. Trade-like stream

Current practical source from the library is the trade-like stream currently available via public API.

Important rule:
- if the data source is semantically `aggTrade`, label it as `aggTrade`
- do not rename it to raw `trade` unless that feed is actually verified and exposed

Why it matters:
- trade / aggTrade is the easiest stream to benchmark first
- it has strong temporal locality
- delta transforms are likely to work well

Typical fields:
- event id
- price
- qty
- side / aggressor info
- timestamp

### 2. bookTicker / L1

This is the `L1 orderbook` stream and one of the three main coursework domains.

Typical fields:
- update id
- best bid price
- best bid qty
- best ask price
- best ask qty
- timestamp

Why it matters:
- high update rate
- small events
- good candidate for delta + generic compression
- useful proxy for live recording pressure
- correct target for specialized level-1 transforms

### 3. Orderbook updates

This is the hardest and most important stream family.

The real problem is not only compression.
The real problem is representation.

For the MVP, the recorder captures **raw `depth@0ms` diff events** (no `@100ms`
throttling): Binance fapi pushes one WS message per book change. The CXETCPP
public entrypoint is `runSubscribeOrderBookDeltaByConfig(cfg, cb)` — a
blocking call that runs on the recorder's own IO thread and invokes
`OrderBookDeltaOnUpdate cb` with a partial `OrderBookSnapshot` (only the
changed levels) per event.

Questions that must be tested:
- store every raw update as received? **(MVP default)**
- store only changed levels?
- store top-N levels only?
- store periodic snapshots plus deltas?
- encode prices relative to midprice or to previous level?

This stream gets its own experimental treatment.

## Stream capture policy

For each stream family, collect real live datasets first.

Target minimum dataset:
- 20 minutes per stream on ETHUSDT futures

Prefer multiple market regimes:
- calm
- normal
- volatile

## Representation candidates by stream

### Trade-like stream

Compare:
- raw row-oriented binary
- delta by event id / timestamp / price
- field-columnar layout
- delta + varint

### bookTicker

Compare:
- raw binary rows
- delta from previous event
- delta from previous bid/ask separately
- per-field column layout
- block-local bit packing

### Orderbook

Compare:
- raw update event
- compact changed-level list
- per-side separate lists
- top-N extracted state
- periodic snapshot + delta chain
- anchor-relative price encoding

## Orderbook-specific notes

Orderbook recording should explicitly test whether the best stored object is:
- the exchange-native update event
- an app-local normalized delta
- a compact reconstruction-oriented representation

The best representation for compression may be different from the best representation for replay.

Possible split:
- archive representation optimized for size
- replay representation optimized for reconstruction speed

## Output naming

All datasets and benchmark outputs must preserve stream semantics clearly:
- `aggTrade`
- `bookTicker`
- `l1_bookticker` where needed in comparison tables
- `orderbook_delta`
- `orderbook_snapshot`

Do not collapse different trade semantics into one generic `trades` label.
