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
