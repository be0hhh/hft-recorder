# hft-recorder - architecture

## Top-level model

`hft-recorder` has three product layers and two support layers.

### Product layers

1. `Capture`
2. `Replay / Validation`
3. `Compression Lab + Dashboard`

### Support layers

1. `CXETCPP bridge`
2. `Qt/QML presentation`

## Capture layer

The capture layer owns:
- session lifecycle
- file creation
- channel writers
- manifest accumulation
- interaction with `CXETCPP`

The capture layer writes only canonical JSON corpus files.

It does not write experimental compressed formats directly.

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
