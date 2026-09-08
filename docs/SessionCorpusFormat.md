# hft-recorder - session corpus format

## Purpose

This document defines the canonical on-disk format for captured sessions.

This is the current source of truth for recording and replay.

Important scope rule:
- JSON is the current canonical durable backend
- JSON is not proof that the upstream source is high-quality or exact
- JSON is not the permanent architecture truth for all future backends
- consumer semantics must stay defined by session/materialization contracts, not by filename-specific behavior alone

Market-data source-quality labels follow
[MARKET_DATA_FEED_QUALITY_AND_SBE](../../../doc/hft-research/MARKET_DATA_FEED_QUALITY_AND_SBE.md).
Every persisted channel must preserve `source_quality`, `source_format`,
`origin`, `feed_kind`, `sequence_policy`, and `timestamp_policy` in manifest or
equivalent session metadata.

## Session directory

Each session lives in:

```text
/mnt/d/recordings/<session_id>/
```

`session_id` format:
- `YYYYMMDD_HHMMSS_<exchange>_<market>_<symbol_or_basket>`

Example:
- `20260418_213000_binance_fapi_ethusdt`

## Required files

```text
manifest.json
jsonl/trades.jsonl
jsonl/bookticker.jsonl
jsonl/depth_tape.jsonl
jsonl/depth_sidecar.jsonl
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
- `capture_contract_version`
- `session_status`
- `identity`
- `capture`
- `arrival_clock`
- `replay`
- `channels`
- `artifacts`
- `runtime_health`
- `integrity`
- `summary`

For each entry under `channels`, metadata must make source quality explicit:
- `source_quality`: `canonical`, `canonical_available_but_aggregated`,
  `degraded`, `diagnostic`, or `unknown`
- `source_format`: for example `json`, `sbe`, `binary`, `rest_depth_seed`, or
  `derived`
- `origin`: exchange/product/channel/raw stream name
- `feed_kind`: for example `raw_trade`, `agg_trade`, `l1_bbo`, or `l2_delta`
- `sequence_policy` and `timestamp_policy`

Important implemented rules:
- current sessions declare schema versions explicitly
- loader validates manifest first, then loads channel files by manifest-declared
  paths
- unknown top-level optional fields in a supported manifest version are ignored
- unsupported schema versions fail deterministically
- there is one current writer and one current reader; older recorder corpora are
  rejected rather than silently migrated or interpreted

The offline backtest boundary is intentionally narrower than viewer/replay
compatibility. New backtests accept only:

- `manifest_schema_version = 3`
- `corpus_schema_version = 3`
- `capture_contract_version = hftrec.captured_arrival_rows_json.v4`
- finalized `complete` sessions with clean integrity and
  `exact_replay_eligible = true`
- manifest-declared current row schemas and paths
- a complete `arrival_clock` summary whose accounted row count equals the
  canonical row count
- at least one application-frame arrival and no row with unavailable arrival

The backtest loader does not search fallback filenames or accept an older
layout. Required strategy channels must be non-empty and have clean runtime
health. Clock anomalies are retained as evidence and do not by themselves make
a corpus unreadable.

Subscription aliases control which upstream fields are requested. They do not
change durable column order: canonical JSON rows always follow the
manifest-declared recorder row schema.

### Arrival tail

Every canonical market row ends with the same fields, in this order:

1. `receive_realtime_ns` from `CLOCK_REALTIME`
2. `receive_monotonic_ns` from `CLOCK_MONOTONIC`
3. `producer_epoch`
4. `source_generation`
5. `session_epoch`
6. `frame_sequence`
7. `shard_sequence`
8. `source_id`
9. `shard_id`
10. `event_ordinal`
11. `arrival_flags`

The sampling boundary is
`hft-parser.application-frame-ready`: the complete application message is
available to the registered parser callback. The two clocks describe the same
arrival boundary. Exchange time remains a separate event field and must never
be substituted for either receive clock.

Arrival flag bits are:

- `1`: application-frame arrival
- `2`: historical REST/archive backfill; seed-only, never a live delivery
- `4`: realtime clock regression
- `8`: monotonic clock did not advance
- `16`: exchange timestamp is ahead of local realtime receive time
- `32`: exchange timestamp is missing

Application arrivals require all identity fields except `event_ordinal` to be
non-zero. Historical rows have zero receive/identity fields and the historical
flag. Application and historical flags are mutually exclusive.

## Channel files

### `trades.jsonl`

One line per normalized trade-like event.

The filename does not define raw-vs-aggregated semantics. Manifest metadata must
say whether the source is `feed_kind=raw_trade`, `feed_kind=agg_trade`, or an
exchange-specific trade kind. Binance FAPI `aggTrade` is
`canonical_available_but_aggregated`, not raw executions.

Current row schema is `cxet_trade_captured_arrival_v1`:

`[price_e8, qty_e8, side, exchange_ts_ns, trade_id, first_trade_id,
last_trade_id, quote_qty_e8, is_buyer_maker, symbol, exchange, market,
capture_seq, ingest_seq, <arrival tail>]`

### `bookticker.jsonl`

One line per normalized level-1 event.

Level-1 BBO channels are observational overlays. If the source is throttled or
bucketed BBO, it must be labelled `degraded` for exact fill/microstructure
claims.

Current row schema is `cxet_bookticker_captured_arrival_v1`:

`[event_id, bid_price_e8, bid_qty_e8, ask_price_e8, ask_qty_e8,
exchange_ts_ns, symbol, exchange, market, capture_seq, ingest_seq,
<arrival tail>]`

### `depth_tape.jsonl` and `depth_sidecar.jsonl`

Current row schema is `cxet_orderbook_tape_rle_captured_arrival_v1`.

Tape row:

`[event_id, tagged_exchange_ts_ns, capture_seq, ingest_seq, <arrival tail>,
price_e8, qty_e8, ...]`

The sidecar has the same `event_id` and tagged exchange timestamp followed by
RLE `(side, count)` pairs. A tape/sidecar mismatch is corruption. Partial or
resynchronized depth recovery is not exact replay and fails closed in the
backtest loader.

`captureSeq` is per-channel and strictly increasing within one persisted channel
file.

`ingestSeq` is session-global and strictly increasing across persisted events
and snapshots. It exists to support deterministic replay ordering when multiple
rows share the same `tsNs`.

The same arrival tail is used by liquidation, mark-price, index-price, funding,
price-limit and candle schemas declared in `manifest.json`. Candles fetched via
REST/archive are historical seed rows and include an explicit duration.

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

Structural validity alone is not backtest eligibility. The stricter exact gate
also requires finalized clean integrity, complete arrival accounting, no
unavailable arrivals and every strategy-required channel in clean runtime
health.

## Replay ordering rule

There are two explicit time planes:

- venue plane: `exchange_ts_ns`, projected onto the replay monotonic coordinate
  and never scheduled after its captured arrival
- strategy delivery plane: `receive_monotonic_ns`, normalized with the session
  realtime/monotonic anchor

Strategy-visible market data is ordered by captured delivery time and then by
`producer_epoch`, `shard_id`, `shard_sequence`, `event_ordinal`, source/session
identity and deterministic channel/index tails. No synthetic market-data
latency is added. Order, cancel and user-data latency remain independently
synthetic execution settings.

Realtime regressions and equal/non-increasing monotonic samples are preserved;
the recorder does not fabricate forward timestamps. Stable capture identity is
the tie-breaker. Result artifacts report both captured clock ranges and replay
coordinate ranges.

Exact book reconstruction semantics:
- exact L2 replay depends on trusted `snapshot` data plus valid `depth` deltas
- `trades.jsonl` and `bookticker.jsonl` are observational overlays
- their presence or absence must not change reconstructed L2 state

## CXET boundary rule

`CXETCPP` callback payloads are a live parser interface, not the durable corpus
contract. The parser-owned application-frame capture feed preserves bounded
native payloads plus both arrival clocks; recorder owns disk I/O and sealing of
the binary corpus. Recorder JSON capture rows remain a separate recorder-owned
durable contract.

Binary and JSON corpora must preserve `source_format`, `origin`, `feed_kind` and
exact channel compatibility. They must not be merged as one anonymous stream or
used as silent fallbacks for one another.
