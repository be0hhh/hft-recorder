# hft-recorder - implementation plan

## Goal

Build `hft-recorder` as a Qt 6 QML desktop application with:
- live capture of normalized market data from `CXETCPP`
- canonical JSON session storage
- replay and validation
- baseline and custom compression lab
- ranking and charts inside the GUI

## Stable architecture

### `src/gui/`

Responsibilities:
- app shell
- QML views
- list models
- viewmodels
- dashboard/chart presentation

### `src/core/capture/`

Responsibilities:
- capture coordinator
- session creation/finalization
- channel file writers
- manifest generation
- bridge to `CXETCPP` public entrypoints

### `src/core/corpus/`

Responsibilities:
- canonical JSON schema
- JSON serialization / parsing
- session loading
- replay iterators

### `src/core/validation/`

Responsibilities:
- original vs decoded comparison
- exactness reporting
- mismatch localization

### `src/core/lab/`

Responsibilities:
- pipeline registry
- benchmark execution
- metric collection
- ranking generation

### `src/variants/`

Responsibilities:
- custom compression candidates
- stream-family-specific experiments

## Phase order

### Phase A. Canonical capture path

Implement:
- session id generation
- session directory layout
- manifest model
- per-channel JSON writers
- Binance FAPI capture through `CXETCPP`
- stop/finalize behavior

Deliverable:
- a session folder with `manifest.json`, `trades.jsonl`, `bookticker.jsonl`,
  `depth.jsonl`, and `snapshot_*.json`

### Phase B. Replay and validation

Implement:
- corpus loader
- normalized replay iterators
- purity comparator
- baseline session stats

Deliverable:
- ability to load any session and replay it in memory deterministically

### Phase C. GUI application

Implement:
- `Qt 6 + QML` shell
- capture page
- sessions page
- validation page
- chart models for trades, L1, and orderbook

Deliverable:
- user can capture a session and inspect it visually

### Phase D. Baseline compression lab

Implement:
- baseline wrappers
- normalized binary derivations from corpus
- lab runner
- result table
- ranking engine

Deliverable:
- compare baselines on real sessions in the GUI

### Phase E. Custom pipeline families

Implement:
- first trade family
- first L1 family
- first orderbook families

Deliverable:
- meaningful custom-vs-baseline comparison

### Phase F. Coursework polish

Implement:
- prettier dashboards
- export formats
- screenshot/defense-friendly reporting
- docs cleanup

## Ground rules

- Canonical truth is the JSON corpus, not experimental pipeline output.
- GUI is not optional.
- Replay and purity checking are mandatory before custom ranking has value.
- Multi-symbol architecture is required, but first fully polished demo may still
  be completed on a single symbol.
