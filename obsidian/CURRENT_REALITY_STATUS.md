# Current Reality Status

Purpose:

- Give agents a trust map before they touch code.
- Separate `implemented`, `usable`, `scaffold`, and `unimplemented`.

Status legend:

- `implemented` = code exists and matches the current product path
- `usable` = real enough for current work, but with known limits
- `scaffold` = shape exists, output is placeholder or partial
- `unimplemented` = explicit stub or not wired

Verified at:

- `2026-04-21`

Reality matrix:

- `capture coordinator`
  - status: `usable`
  - verified_against: `src/core/capture/CaptureCoordinator.cpp`
  - notes: real threads and JSON writers exist; current live path is effectively Binance FAPI and one symbol per coordinator

- `capture gui facade`
  - status: `usable`
  - verified_against: `src/gui/viewmodels/CaptureViewModel.cpp`
  - notes: multi-coordinator batch exists; product wording is broader than current live CXET binding

- `capture cli`
  - status: `usable`
  - verified_against: `src/app/capture_cli.cpp`
  - notes: support path only; hardcoded default scope

- `session corpus writers`
  - status: `usable`
  - verified_against: `src/core/capture/ChannelJsonWriter.cpp`, `src/core/capture/JsonSerializers.cpp`, `src/core/capture/SessionManifest.cpp`
  - notes: canonical files are written; escaping and schema-hardening are still open

- `replay loader`
  - status: `usable`
  - verified_against: `src/core/replay/SessionReplay.cpp`
  - notes: active path for viewer; loads the current JSON corpus and supports the current replay/viewer baseline, but live chart-axis presentation still needs verification

- `json replay parser`
  - status: `usable`
  - verified_against: `src/core/replay/JsonLineParser.cpp`
  - notes: handwritten parser works for current happy path; fragile seam and high-priority audit area

- `book state / viewer replay`
  - status: `usable`
  - verified_against: `src/core/replay/BookState.cpp`, `src/gui/viewer/ChartController.cpp`
  - notes: real and central; orderbook rendering is clipped to real session time coverage and supports selection-summary-driven viewer work, but scale/graph synchronization in the live GUI is still an open runtime issue

- `gui viewer`
  - status: `usable`
  - verified_against: `src/gui/viewer/ChartController.cpp`, `src/gui/qml/views/ViewerView.qml`
  - notes: active product surface; selection rectangle, compact summary overlay, dollar-based book controls, and persistent top controls are all real, but recent live screenshots show axes visually detached from the rendered chart so the presentation layer is not yet fully trustworthy

- `session list / browsing`
  - status: `usable`
  - verified_against: `src/gui/models/SessionListModel.cpp`
  - notes: real support path around the corpus

- `validation core`
  - status: `implemented`
  - verified_against: `src/core/validation/ValidationRunner.cpp`
  - notes: basic compare path exists; richer mismatch reporting is still behind docs

- `lab runner`
  - status: `scaffold`
  - verified_against: `src/core/lab/LabRunner.cpp`
  - notes: emits baseline-like placeholder results derived from corpus shape

- `ranking engine`
  - status: `implemented`
  - verified_against: `src/core/lab/RankingEngine.cpp`
  - notes: sorting path exists; depends on upstream results being real

- `bench cli`
  - status: `scaffold`
  - verified_against: `src/app/benchmark_cli.cpp`
  - notes: support binary exists; output says skeleton only

- `analyze cli`
  - status: `unimplemented`
  - verified_against: `src/app/analyze_cli.cpp`
  - notes: placeholder text still references old `.cxrec` direction

- `report export cli`
  - status: `unimplemented`
  - verified_against: `src/app/report_export.cpp`
  - notes: placeholder text still references pushgateway-era reporting

- `cxet bridge seam`
  - status: `implemented`
  - verified_against: `src/core/cxet_bridge/CxetCaptureBridge.cpp`
  - notes: row-mapping helpers for trades, bookTicker, and orderbook snapshots are real and used by `CaptureCoordinatorRuntime`, but the bridge is still a support seam rather than the architectural center of capture

- `metrics module`
  - status: `scaffold`
  - verified_against: `src/core/metrics/Metrics.cpp`
  - notes: no-op stub; docs are much richer than implementation

- `support external wrappers`
  - status: `scaffold`
  - verified_against: `src/support/external_wrappers/*`
  - notes: mostly `Status::Unimplemented`

- `variants`
  - status: `scaffold`
  - verified_against: `src/variants/*`
  - notes: shape and naming exist; not current implementation truth

- `block / codec path`
  - status: `scaffold`
  - verified_against: `src/core/block/*`, `src/core/codec/*`
  - notes: historical or future-facing; not the active JSON-corpus center

Current product center:

1. `CaptureCoordinator`
2. `SessionReplay`
3. `ChartController`
4. session corpus on disk

Do not overtrust:

- `src/core/cxet_bridge`
- `src/core/metrics`
- `src/core/block`
- `src/variants`
- CLI placeholder copy
