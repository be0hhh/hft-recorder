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
- `source_quality=canonical` only after initial depth image and sequence/gap
  validation are proven for the session

Stored in:
- `depth_tape.jsonl`
- `depth_sidecar.jsonl`

The initial full-book snapshot, when available, is stored as a depth row. There
is no standalone snapshot artifact in the current corpus contract.

## Research families

Compression work is still organized into three families:
- trade-like
- L1 / bookTicker
- orderbook

Initial full-depth rows are supporting truth data for orderbook reconstruction, not a fourth
independent ranking family.

## Replay semantics

Canonical replay time is the row `tsNs`.

Algo-facing replay semantics are defined by
`REPLAY_TO_CXETCPP_FANOUT.md`. The important distinction is that file/channel
loading order is not the market-data order seen by algorithms.

Replay is bucketed by timestamp:
- all rows with the same `tsNs` belong to one replay moment
- same-`tsNs` rows are rendered at the same visual position/frame
- there is no visible channel priority between channels for equal timestamps
- algo-facing delivery must merge channel rows by `tsNs`, then `ingestSeq`, then
  a stable tie-breaker before CXETCPP fanout

State reconstruction rules:
- exact L2 reconstruction is driven by depth rows; the first full-depth row seeds
  the book when available
- `bookticker` is observational L1 data
- `trades` are observational tape data
- `bookticker` and `trades` must not mutate reconstructed L2 state

Deterministic internal ordering may still exist inside a bucket for
reproducibility, but it is not a semantic precedence contract.
