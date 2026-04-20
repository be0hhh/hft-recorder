# hft-recorder - API contracts

## Stable backend contracts to define early

The following backend types are part of the new stable direction.

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

The seam does not own:
- transport internals
- exchange-specific runtime behavior
- replay ordering or integrity policy

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

`SessionReplay` should consume the same structural loader verdict rather than
re-deriving file-level policy independently.

They should not depend on:
- direct `CXETCPP` callback structs
- live exchange metadata lookups for core identity and scale facts
- separate backtest-only truth formats
