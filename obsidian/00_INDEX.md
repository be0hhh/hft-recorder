# hft-recorder Agent Map

This vault is a fast map for agents.

Read in this order:

1. [[01_RUNTIME_GRAPH]]
2. [[03_CAPTURE]]
3. [[04_REPLAY_VALIDATION]]
4. [[05_GUI_VIEWER]]
5. [[06_LAB_VARIANTS]]
6. [[07_BUILD_TEST_BOUNDARIES]]
7. [[02_FILE_ROLES]]
8. [[09_AGENT_STACK]]
9. [[10_TASKS]]
10. [[13_DECISIONS]]
11. [[14_GOTCHAS]]
12. [[15_VECTOR_RETRIEVAL]]
13. [[16_MCP_STACK]]
14. [[08_GRAPHIFY_LAYER]]

Core idea:

- `hft-recorder` is a GUI-first capture, replay, validation, and compression lab on top of prebuilt `CXETCPP`.
- The canonical truth artifact is a session folder with `manifest.json`, `trades.jsonl`, `bookticker.jsonl`, `depth.jsonl`, and snapshots.
- `src/core/` owns backend logic.
- `src/gui/` owns QML, viewmodels, and chart rendering.
- `src/app/` owns CLI entrypoints.
- `src/variants/` and `src/support/` own compression experiments and external codec wrappers.

Current reality worth remembering:

- Capture is real and centered on `CaptureCoordinator`.
- Replay/viewer path is real and centered on `SessionReplay` and `ChartController`.
- Lab/ranking path exists but is still mostly baseline/scaffold quality.
- `CxetCaptureBridge` is present but currently returns `Status::Unimplemented`.
- `StreamRecorder` and several codec/variant areas are transitional or placeholder-level.

Canonical docs outside this vault:

- `doc/OVERVIEW.md`
- `doc/ARCHITECTURE.md`
- `doc/SESSION_CORPUS_FORMAT.md`
- `doc/SOURCE_LAYOUT_AND_VARIANTS.md`
- `doc/VALIDATION_AND_RANKING.md`
- `doc/CONFIG_AND_CLI.md`

Machine layer:

- [[08_GRAPHIFY_LAYER]]

Workflow layer:

- [[09_AGENT_STACK]]
- [[10_TASKS]]
- [[11_ACTIVE]]
- [[12_BACKLOG]]
- [[13_DECISIONS]]
- [[14_GOTCHAS]]
- [[15_VECTOR_RETRIEVAL]]
- [[16_MCP_STACK]]
