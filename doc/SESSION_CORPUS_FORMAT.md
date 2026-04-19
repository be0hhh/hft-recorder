# hft-recorder - session corpus format

## Purpose

This document defines the canonical on-disk format for captured sessions.

This is the current source of truth for recording and replay.

## Session directory

Each session lives in:

```text
recordings/<session_id>/
```

`session_id` format:
- `YYYYMMDD_HHMMSS_<exchange>_<market>_<symbol_or_basket>`

Example:
- `20260418_213000_binance_fapi_ethusdt`

## Required files

```text
manifest.json
trades.jsonl
bookticker.jsonl
depth.jsonl
snapshot_000.json
snapshot_001.json
...
```

Optional subdirectories:

```text
derived/
reports/
logs/
```

## Manifest

Current implemented manifest fields are:
- `session_id`
- `exchange`
- `market`
- `symbols`
- `selected_parent_dir`
- `started_at_ns`
- `ended_at_ns`
- `target_duration_sec`
- `actual_duration_sec`
- `snapshot_interval_sec`
- `channel_status`
- `event_counts`
- `warning_summary`

Fields like `app_version`, `cxetcpp_version`, separate warning/error maps, and
other richer metadata remain planned but are not the current on-disk contract.

`manifest.json` stores session metadata for the current runtime.

## Channel files

### `trades.jsonl`

One line per normalized trade-like event.

Current implemented fields:
- `tsNs`
- `priceE8`
- `qtyE8`
- `sideBuy`

The richer normalized trade schema described in older docs is not yet what the
recorder writes today.

### `bookticker.jsonl`

One line per normalized level-1 event.

Current implemented fields:
- `tsNs`
- `bidPriceE8`
- `askPriceE8`

Optional current fields:
- `bidQtyE8`
- `askQtyE8`

### `depth.jsonl`

One line per normalized orderbook delta event.

Current implemented fields:
- `tsNs`
- `updateId`
- `firstUpdateId`
- `bids`
- `asks`

Each `bids` / `asks` item:
- `price_i64`
- `qty_i64`

These sequence ids are the current replay integrity seam for orderbook gap
validation.

### `snapshot_NNN.json`

One file per full normalized snapshot.

Current implemented fields:
- `tsNs`
- `updateId`
- `firstUpdateId`
- `bids`
- `asks`

Each `bids` / `asks` item:
- `price_i64`
- `qty_i64`

## Numeric representation

All price, quantity, timestamp, and id values are stored as native JSON numbers.

Rules:
- values remain integer-based
- no `double`
- no humanized decimal strings in canonical corpus
- external JS-only tools may lose precision above `2^53`; this is acceptable
  because the canonical supported loaders are C++ and Python

## Schema rule

The canonical schema describes the normalized events currently emitted by the
recorder, not the larger aspirational schema from older planning docs.

This is intentional:
- cleaner replay
- stable benchmark input
- simpler exactness comparison
- easier future backtest integration
