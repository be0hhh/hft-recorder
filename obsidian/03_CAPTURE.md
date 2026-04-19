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
- `CxetCaptureBridge` is not the active path; `CaptureCoordinator` talks to CXET APIs directly.

If you debug capture:

- start at `CaptureCoordinator.cpp`
- then read `CaptureViewModel.cpp`
- then inspect `JsonSerializers.cpp`

