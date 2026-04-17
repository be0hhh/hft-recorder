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
- aggregate stats
- warnings / errors

### `ChannelJsonWriter`

Responsibilities:
- open the correct channel file
- serialize normalized records into canonical JSON
- flush and finalize safely

## Corpus

### `CorpusLoader`

Responsibilities:
- load one session directory
- parse channel JSON files
- return normalized in-memory records

### `SessionCorpus`

Holds:
- manifest
- trade records
- book ticker records
- depth delta records
- snapshot records

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
