# hft-recorder - dataset capture protocol

## Purpose

This document defines how to capture comparable datasets for the coursework.

The same protocol should be used for all future measurements so results stay reproducible.

## Primary symbol and market

Default primary corpus:
- symbol: `ETHUSDT`
- market: Binance futures

Use honest stream labels:
- `aggTrade` if that is what the current library exposes
- `bookTicker`
- `orderbook_updates`

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

## Online follow-up

Once promising methods are identified offline, the same stream family should be recorded again through the true online compression path to validate:
- sustained throughput
- CPU cost
- block flush behavior
- live safety margin
