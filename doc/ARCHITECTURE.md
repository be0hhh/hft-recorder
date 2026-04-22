# hft-recorder - architecture

## Top-level model

`hft-recorder` has four domain layers and two support layers.

### Domain layers

1. `Storage / Corpus`
2. `Market data ingress / Replay / Validation`
3. `Execution venue`
4. `Compression Lab + Dashboard / Presentation`

### Support layers

1. `CXETCPP bridge`
2. `Qt/QML presentation`

## Storage / corpus layer

The storage layer owns the canonical session truth:
- append sinks for normalized rows
- hot in-memory cache over the same row schema
- backend-specific durable writers such as JSON session storage

Current thin seams:
- `storage::IHotEventCache`
- `storage::IStorageBackend`
- `storage::LiveEventStore`
- `storage::JsonSessionSink`

Hot memory is a front-cache, not a separate schema.

## Capture layer

The capture layer owns:
- session lifecycle
- file creation
- channel writers
- manifest accumulation
- interaction with `CXETCPP`

The capture layer writes only canonical JSON corpus files.

It does not write experimental compressed formats directly.

Capture is also the first `market_data::IMarketDataIngress` implementation:
- it owns CXET callbacks
- it maps them into recorder-owned normalized rows
- it exposes a hot event source for presentation without binding the UI to file tailing

## Replay and validation layer

This layer loads the canonical corpus and reconstructs ordered normalized event
streams.

It is used by:
- validation views
- charts
- compression benchmarks
- accuracy checks
- future backtest adapters

## Compression lab layer

The lab layer runs:
- baseline compression pipelines
- custom C++ variants
- optional Python-side research outputs imported back into reports

The lab layer does not replace the canonical corpus.
It consumes it.

## Execution venue layer

The local execution venue is a separate domain from market-data capture.

Current thin seams:
- `execution::IExecutionVenue`
- `execution::IExecutionEventSink`
- `execution::IExecutionEventSource`

`LocalOrderEngine` is the first venue implementation behind these seams.
It may publish normalized execution events into recorder-owned stores without exposing socket-frame internals upstream.

## Qt/QML boundary

The application boundary is:
- C++ backend for data and logic
- QML frontend for UI and charts

QML talks to:
- viewmodels
- models
- controller facades

QML never owns:
- capture logic
- file I/O
- benchmark execution
- ranking logic

## CXETCPP boundary

`hft-recorder` uses only:
- prebuilt `libcxet_lib.so`
- public headers
- public request / stream / run APIs

It must not include:
- `network/`
- `parse/`
- `exchanges/`
- `runtime/`

## Source tree direction

```text
src/
  gui/
  core/
    capture/
    corpus/
    validation/
    lab/
    cxet_bridge/
    common/
    metrics/
  support/
  variants/
```

Old CLI-first files remain transitional until fully migrated or deleted.
