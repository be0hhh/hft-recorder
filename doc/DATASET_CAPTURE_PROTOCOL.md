# hft-recorder - dataset capture protocol

## Purpose

This document defines how to capture comparable datasets for the coursework.

The same protocol should be used for all future measurements so results stay reproducible.

Source-quality policy follows
[MARKET_DATA_FEED_QUALITY_AND_SBE](../../../doc/hft-research/MARKET_DATA_FEED_QUALITY_AND_SBE.md).
The current canonical JSON corpus is a durable backend; it is not by itself a
claim that the upstream feed is exact or high-quality.

## Primary symbol and market

Default primary corpus:
- symbol: `ETHUSDT`
- market: Binance futures

Use honest stream labels:
- `aggTrade` if that is what the current library exposes
- `bookTicker`
- `orderbook_updates`

Do not use unnamed `trades`: choose `raw_trade`, `agg_trade`, or a documented
exchange-specific feed kind.

## Target capture windows

Minimum target:
- `20 minutes` per stream family

Preferred capture sets:
- calm period
- normal period
- volatile period

## Capture outputs

For each session store:
- stream family
- market / exchange
- symbol
- `source_quality`
- `source_format`
- `origin`
- `feed_kind`
- `sequence_policy`
- `timestamp_policy`
- start timestamp
- duration
- approximate event count
- semantic notes (`aggTrade` vs raw trade)
- whether the file is raw, transformed, or compressed

## File naming guidance

Use explicit names such as:

```text
ETHUSDT_binance_fapi_aggTrade_20m_normal.rawbin
ETHUSDT_binance_fapi_bookTicker_20m_normal.rawbin
ETHUSDT_binance_fapi_orderbook_updates_20m_normal.rawbin
```

If multiple representations exist, add a suffix:

```text
..._topN25.repbin
..._delta_varint.repbin
..._columnar.repbin
```

## Capture policy

- Capture the same stream family once, then reuse the same corpus for many benchmark runs.
- Do not mix different market conditions in the same benchmark label.
- Preserve enough metadata to explain later why one run differs from another.
- Degraded BBO or snapshot-only data must not be used for exact fill claims.
- If future SBE/binary capture is added, materialize it into canonical rows only
  while preserving `source_format=sbe` or the concrete binary protocol,
  `origin`, and `feed_kind`.

## Online follow-up

Once promising methods are identified offline, the same stream family should be recorded again through the true online compression path to validate:
- sustained throughput
- CPU cost
- block flush behavior
- live safety margin
