# hft-recorder Agent Map

This vault is a fast map for agents.

Read in this order:

1. [[START_HERE_TRUTH_ORDER]]
2. [[CURRENT_REALITY_STATUS]]
3. [[HISTORICAL_VS_ACTIVE]]
4. [[SAFE_WORK_RULES]]
5. [[01_RUNTIME_GRAPH]]
6. [[03_CAPTURE]]
7. [[04_REPLAY_VALIDATION]]
8. [[05_GUI_VIEWER]]
9. [[17_VIEWER_BASELINE_2026_04]]
10. [[CAPTURE_SEMANTICS_AND_SCHEMA_BRIDGE]]
11. [[REVIEW_BACKLOG]]
12. [[OBSERVABILITY_PLAN]]
13. [[06_LAB_VARIANTS]]
14. [[07_BUILD_TEST_BOUNDARIES]]
15. [[BUILD_RUN_TEST_MATRIX]]
16. [[02_FILE_ROLES]]
17. [[09_AGENT_STACK]]
18. [[10_TASKS]]
19. [[13_DECISIONS]]
20. [[14_GOTCHAS]]
21. [[15_VECTOR_RETRIEVAL]]
22. [[16_MCP_STACK]]
23. [[08_GRAPHIFY_LAYER]]

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
- Docs about metrics, block format, and some CLI paths can overstate implementation maturity.

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
