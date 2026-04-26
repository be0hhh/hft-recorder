# hft-recorder - streams

## First supported source

The first supported live source is:
- `Binance FAPI`

Source-quality policy is defined in shared HFT research docs:
[MARKET_DATA_FEED_QUALITY_AND_SBE](../../../doc/hft-research/MARKET_DATA_FEED_QUALITY_AND_SBE.md).
In this file, canonical channel means durable corpus channel, not proof that the
upstream exchange feed is exact microstructure truth.

Every channel must carry `source_quality`, `source_format`, `origin`,
`feed_kind`, `sequence_policy`, and `timestamp_policy`.

## Canonical channels

### Trades

Current truth:
- capture the trade-like feed exposed by `CXETCPP`
- if the feed is semantically `aggTrade`, it must be labelled honestly
- Binance FAPI current trade-like source is `feed_kind=agg_trade`
- quality label is `canonical_available_but_aggregated` unless a verified raw
  trade source is added later

Stored in:
- `trades.jsonl`

### BookTicker

Treat this as:
- `L1 orderbook`
- `source_quality=degraded` for exact fill/microstructure claims unless a
  stronger verified book source is used

Stored in:
- `bookticker.jsonl`

### Depth delta

Capture:
- normalized raw orderbook delta events
- `source_quality=canonical` only after snapshot anchoring and sequence/gap
  validation are proven for the session

Stored in:
- `depth.jsonl`

### Snapshot

Capture:
- full normalized snapshots
- one at start
- periodic refreshes every 60 s
- snapshots are anchors/supporting truth, not tick-by-tick market truth by
  themselves

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
