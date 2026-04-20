# hft-recorder - streams

## First supported source

The first supported live source is:
- `Binance FAPI`

## Canonical channels

### Trades

Current truth:
- capture the trade-like feed exposed by `CXETCPP`
- if the feed is semantically `aggTrade`, it must be labelled honestly

Stored in:
- `trades.jsonl`

### BookTicker

Treat this as:
- `L1 orderbook`

Stored in:
- `bookticker.jsonl`

### Depth delta

Capture:
- normalized raw orderbook delta events

Stored in:
- `depth.jsonl`

### Snapshot

Capture:
- full normalized snapshots
- one at start
- periodic refreshes every 60 s

Stored in:
- `snapshot_*.json`

## Research families

Compression work is still organized into three families:
- trade-like
- L1 / bookTicker
- orderbook

Snapshots are supporting truth data for orderbook reconstruction, not a fourth
independent ranking family.

## Replay semantics

Canonical replay time is the row `tsNs`.

Replay is bucketed by timestamp:
- all rows with the same `tsNs` belong to one replay moment
- same-`tsNs` rows are rendered at the same visual position/frame
- there is no visible channel priority between channels for equal timestamps

State reconstruction rules:
- exact L2 reconstruction is driven by `snapshot + depth`
- `bookticker` is observational L1 data
- `trades` are observational tape data
- `bookticker` and `trades` must not mutate reconstructed L2 state

Deterministic internal ordering may still exist inside a bucket for
reproducibility, but it is not a semantic precedence contract.
