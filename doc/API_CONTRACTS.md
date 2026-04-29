# hft-recorder - API contracts

## Stable backend contracts to define early

For the full `hftrecorder_local` venue direction, including replay fanout,
private streams, balance, positions, fees, funding and local auth, use the
canonical `LOCAL_EXCHANGE_*` documents. This file defines the broader backend
seams and remains compatible with that direction.

The following backend types are part of the new stable direction.

### `storage::IHotEventCache`

Responsibilities:
- accept normalized market-data rows on the hot path
- expose exact in-memory reads by full scan or time range
- preserve the same row schema used by durable storage

### `storage::IStorageBackend`

Responsibilities:
- persist normalized rows into a durable backend
- report backend identity and append stats
- stay behind the same append contract as the hot cache

### `market_data::IMarketDataIngress`

Responsibilities:
- represent any live or replay-driven market-data ingress
- expose a presentation-safe event source
- expose the current hot cache without leaking transport details

### `execution::IExecutionVenue`

Responsibilities:
- accept normalized order intents from the CXET local venue seam
- publish recorder-owned execution events through an event sink
- keep execution behavior separate from viewer/storage policy

Reserved execution semantic family:
- `ack`
- `reject`
- `state change`
- future `fill`
- future `account change`
- future `position change`

The current socket wire contract may remain `OrderAck`-only, but the internal
execution domain must remain additive beyond transport ack semantics.

## Capture

### `CaptureCoordinator`

Responsibilities:
- create session directories
- start and stop channel capture
- update manifest state
- expose live counters to the GUI

### `SessionManifest`

Represents:
- session metadata
- schema/version contract for the session corpus
- aggregate stats
- warnings / structural blockers
- canonical artifact inventory and replay structural eligibility
- capture contract version

### `InstrumentMetadata`

Represents:
- stable per-session instrument identity
- scale facts needed by canonical consumers
- optional microstructure facts known at capture time
- explicit unknown values for unavailable facts

### `ChannelJsonWriter`

Responsibilities:
- open the correct channel file
- serialize normalized records into canonical JSON
- flush and finalize safely

## Corpus

### `CorpusLoader`

Responsibilities:
- load one session directory
- validate `manifest.json` first
- enforce supported schema versions
- parse channel JSON files by manifest-declared artifact paths
- return normalized in-memory records
- return structured `LoadReport` issues for fatal, degraded, and warning cases
- discover and validate optional seek sidecars without promoting them to truth

### `SessionCorpus`

Holds:
- manifest
- optional parsed instrument metadata
- trade records
- book ticker records
- depth delta records
- snapshot records
- optional support-artifact documents

## Recorder-facing CXET seam

The recorder should not treat raw `CXETCPP` callback shapes as its durable
corpus contract.

Recorder-owned seam responsibilities:
- map `CXETCPP` callback payloads into recorder-owned normalized DTOs
- serialize recorder DTOs into canonical JSON
- map capture failures into recorder-owned failure events/messages
- preserve one identity model for live/history consumers: `exchange + market + symbol + sourceId`

The seam does not own:
- transport internals
- exchange-specific runtime behavior
- replay ordering or integrity policy

## Local CXET venue seam

`hftrecorder_local` is a CXET exchange id used for local algorithm order intents.

Current v1 contract:
- algorithm uses existing `sendWs().object(order).exchange(hftrecorder_local)`
- CXET sends a binary normalized order frame over Unix domain socket
- hft-recorder owns the local socket server
- hft-recorder returns `OrderAck` on the socket path
- internal recorder modules may also consume recorder-owned normalized execution events
- no matching, PnL, replay feed, or chart drawing is implied by the transport contract alone

Socket path:
- `CXET_HFTREC_SOCKET` when set
- `/tmp/cxet-hftrecorder-local.sock` by default

This path is intentionally local-only and must not be used as a real exchange
transport replacement for production venues.

## Validation

### `ValidationRunner`

Responsibilities:
- compare original corpus records vs decoded records
- emit mismatch summary
- compute accuracy class

### `ValidationResult`

Contains:
- total events
- exact matches
- mismatches
- accuracy fraction
- first mismatch location

## Lab

### `PipelineDescriptor`

Describes:
- id
- stream family
- representation strategy
- codec strategy
- profile

### `LabRunner`

Responsibilities:
- run one or more pipelines on a session corpus
- collect timing and size metrics
- invoke validation

### `PipelineResult`

Contains:
- pipeline id
- input bytes
- output bytes
- ratio
- encode/decode timings
- accuracy class
- accuracy fraction

### `RankingEngine`

Responsibilities:
- rank pipelines per stream family and profile
- generate GUI-friendly result tables

## Qt-facing boundary

The Qt layer talks to backend viewmodels and models, not directly to low-level
capture or codec classes.

## Future consumer boundary

Future replay/backtest consumers should read:
- `manifest.json`
- canonical channel files
- `instrument_metadata.json`
- optional advisory support artifacts

They should read those through storage/materialization contracts, not through
hardcoded JSON-tail behavior as the long-term architecture truth.

`SessionReplay` should consume the same structural loader verdict rather than
re-deriving file-level policy independently.

They should not depend on:
- direct `CXETCPP` callback structs
- live exchange metadata lookups for core identity and scale facts
- separate backtest-only truth formats
