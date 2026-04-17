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

`manifest.json` stores session metadata:
- `session_id`
- `app_version`
- `cxetcpp_version`
- `exchange`
- `market`
- `symbols`
- `started_at_ns`
- `ended_at_ns`
- `target_duration_sec`
- `actual_duration_sec`
- `snapshot_interval_sec`
- `channels`
- `event_counts`
- `warning_counts`
- `error_counts`

## Channel files

### `trades.jsonl`

One line per normalized trade-like event.

Required fields:
- `session_id`
- `channel`
- `exchange`
- `market`
- `symbol`
- `event_index`
- `event_time_ns`
- `trade_time_ns`
- `trade_id`
- `price_i64`
- `qty_i64`
- `side`
- `is_aggregated`

### `bookticker.jsonl`

One line per normalized level-1 event.

Required fields:
- `session_id`
- `channel`
- `exchange`
- `market`
- `symbol`
- `event_index`
- `event_time_ns`
- `update_id`
- `best_bid_price_i64`
- `best_bid_qty_i64`
- `best_ask_price_i64`
- `best_ask_qty_i64`

### `depth.jsonl`

One line per normalized orderbook delta event.

Required fields:
- `session_id`
- `channel`
- `exchange`
- `market`
- `symbol`
- `event_index`
- `event_time_ns`
- `first_update_id`
- `final_update_id`
- `bids`
- `asks`

Each `bids` / `asks` item:
- `price_i64`
- `qty_i64`

### `snapshot_NNN.json`

One file per full normalized snapshot.

Required fields:
- `session_id`
- `channel`
- `exchange`
- `market`
- `symbol`
- `snapshot_index`
- `snapshot_time_ns`
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

The canonical schema describes normalized `CXETCPP` events, not raw exchange
messages.

This is intentional:
- cleaner replay
- stable benchmark input
- simpler exactness comparison
- easier future backtest integration
