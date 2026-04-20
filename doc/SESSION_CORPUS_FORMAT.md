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

Additional canonical/support sidecars now standardized:

```text
instrument_metadata.json
seek_index.json
reports/session_audit.json
reports/integrity_report.json
reports/loader_diagnostics.json
```

## Manifest

`manifest.json` is the structural entrypoint for the session corpus.

Current canonical top-level groups:
- `manifest_schema_version`
- `corpus_schema_version`
- `session_status`
- `identity`
- `capture`
- `replay`
- `channels`
- `snapshots`
- `artifacts`
- `summary`

Important implemented rules:
- new-format sessions declare schema versions explicitly
- loader validates manifest first, then loads channel files by manifest-declared
  paths
- unknown top-level optional fields in a supported manifest version are ignored
- unsupported schema versions fail deterministically
- older flat manifests remain loadable as `legacy_v0`

Current `legacy_v0` compatibility path covers older manifests with flat fields:
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

## Channel files

### `trades.jsonl`

One line per normalized trade-like event.

Current implemented fields:
- `tsNs`
- `captureSeq`
- `ingestSeq`
- `priceE8`
- `qtyE8`
- `sideBuy`

The richer normalized trade schema described in older docs is not yet what the
recorder writes today.

### `bookticker.jsonl`

One line per normalized level-1 event.

Current implemented fields:
- `tsNs`
- `captureSeq`
- `ingestSeq`
- `bidPriceE8`
- `askPriceE8`

Optional current fields:
- `bidQtyE8`
- `askQtyE8`

### `depth.jsonl`

One line per normalized orderbook delta event.

Current implemented fields:
- `tsNs`
- `captureSeq`
- `ingestSeq`
- `updateId`
- `firstUpdateId`
- `bids`
- `asks`

Each `bids` / `asks` item:
- `price_i64`
- `qty_i64`

These sequence ids are the current replay integrity seam for orderbook gap
validation.

`captureSeq` is per-channel and strictly increasing within one persisted channel
file.

`ingestSeq` is session-global and strictly increasing across persisted events
and snapshots. It exists to support deterministic replay ordering when multiple
rows share the same `tsNs`.

### `snapshot_NNN.json`

One file per full normalized snapshot.

Current implemented fields:
- `tsNs`
- `captureSeq`
- `ingestSeq`
- `updateId`
- `firstUpdateId`
- `snapshotKind`
- `source`
- `exchange`
- `market`
- `symbol`
- `sourceTsNs`
- `ingestTsNs`
- `anchorUpdateId`
- `anchorFirstUpdateId`
- `trustedReplayAnchor`
- `bids`
- `asks`

Each `bids` / `asks` item:
- `price_i64`
- `qty_i64`

Snapshot provenance fields make replay anchoring explicit rather than inferred
only from filename or directory traversal order.

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

## Instrument metadata sidecar

`instrument_metadata.json` stores stable instrument facts required by replay and
future backtest consumers without forcing a live exchange metadata lookup.

Current contract:
- one file per session
- recorder-owned schema
- explicit `null` for unknown values
- source tags for populated facts

Current fields include:
- `exchange`, `market`, `symbol`
- `instrument_type`
- `base_asset`, `quote_asset`, `settlement_asset`
- `price_scale_digits`, `qty_scale_digits`
- optional `tick_size_e8`, `lot_size_e8`, `instrument_status`

## Support artifacts

Support artifacts under `reports/` are advisory only. They never redefine
canonical truth.

Standardized support artifacts:
- `seek_index.json`
- `reports/session_audit.json`
- `reports/integrity_report.json`
- `reports/loader_diagnostics.json`

Rules:
- absence of a support artifact does not invalidate the session
- support artifacts may summarize or cache findings
- stale `seek_index.json` must be ignored deterministically
- replay and future backtest consumers must still treat canonical JSON files and
  `instrument_metadata.json` as the durable truth inputs

## Structural validity rule

A session is structurally loadable only if:
- `manifest.json` exists and parses
- manifest and corpus schema versions are supported
- `session_id`, `exchange`, `market`, and `symbols` are present
- every manifest-declared required artifact exists
- disabled channels are not treated as missing required artifacts

This document intentionally defines only structural validity for the current
phase. Richer integrity, gap, and degraded/corrupt semantics are separate
follow-up work.

## Replay ordering rule

The canonical replay timestamp is `tsNs`.

The canonical merged replay unit is a timestamp bucket:
- every row with the same `tsNs` belongs to one replay bucket
- `seek(tsNs)` applies all buckets with `bucket.tsNs <= tsNs`
- equal-timestamp rows do not imply visible channel precedence

Exact book reconstruction semantics:
- exact L2 replay depends on trusted `snapshot` data plus valid `depth` deltas
- `trades.jsonl` and `bookticker.jsonl` are observational overlays
- their presence or absence must not change reconstructed L2 state

## CXET boundary rule

`CXETCPP` callback payloads are a live capture interface, not the durable corpus
contract. Recorder code should translate them into recorder-owned normalized
capture rows before writing canonical JSON.
