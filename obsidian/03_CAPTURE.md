# Capture

Owns:

- Session creation.
- Session folder naming.
- Channel start/stop.
- Writing canonical JSON corpus files.
- Live interaction with public `CXETCPP` capture APIs.

Main objects:

- `CaptureCoordinator`
- `ChannelJsonWriter`
- `JsonSerializers`
- `SessionManifest`
- `SessionId`
- `CaptureViewModel`
- `CaptureViewModelRequests`
- `CaptureViewModelBatch`
- `CaptureViewModelState`

Actual flow:

1. `CaptureViewModel` or `capture_cli.cpp` builds `CaptureConfig`.
2. `CaptureCoordinator::ensureSession()` creates `recordings/<session_id>/`.
3. `startTrades()`, `startBookTicker()`, and `startOrderbook()` open writers and launch threads.
4. Capture threads read normalized data from `cxet_lib` public APIs.
5. Serializers write `trades.jsonl`, `bookticker.jsonl`, `depth.jsonl`, and `snapshot_XXX.json`.
6. `finalizeSession()` stops threads, closes files, and writes final manifest state.

Files written:

- `manifest.json`
- `trades.jsonl`
- `bookticker.jsonl`
- `depth.jsonl`
- `snapshot_000.json` and later snapshots

Depends on:

- public CXET headers and `cxet_lib`
- filesystem
- `JsonSerializers`

Used by:

- `CaptureViewModel`
- `capture_cli.cpp`
- later replay/lab flows through the generated session folder

Important current facts:

- Current capture target is effectively Binance futures style flow centered on normalized CXET APIs.
- `CaptureViewModel` creates one `CaptureCoordinator` per normalized symbol.
- `CaptureViewModel` is now split by concern:
  - facade/QML signal glue in `CaptureViewModel.cpp`
  - symbol + alias + request DSL policy in `CaptureViewModelRequests.cpp`
  - multi-coordinator start/stop/finalize supervision in `CaptureViewModelBatch.cpp`
  - polling/state aggregation in `CaptureViewModelState.cpp`
- `CaptureView.qml` is also split structurally:
  - top-level screen composition stays in `CaptureView.qml`
  - reusable cards/buttons/chips live under `src/gui/qml/components/Capture*.qml`
- `CxetCaptureBridge` is not the active path; `CaptureCoordinator` talks to CXET APIs directly.

If you debug capture:

- start at `CaptureCoordinator.cpp`
- then read `CaptureViewModelBatch.cpp`
- then read `CaptureViewModelRequests.cpp`
- then inspect `JsonSerializers.cpp`
