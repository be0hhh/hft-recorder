# Graphify Entrypoints

Purpose:

- Give a practical way to use the refreshed `graphify-out` without relying on the noisy global graph or the stale generated canvas layout.
- Keep entrypoints focused on active product paths: capture, replay, viewer, and manifest/session loading.

Freshness:

- raw graph verified against `graphify-out/GRAPH_REPORT.md`
- raw graph refresh date: `2026-04-22`
- raw graph summary: `176 files`, `927 nodes`, `1247 edges`, `159 communities`
- note: `graphify-generated/` pages still come from the previous export snapshot, so symbol pages are useful, but community numbering and canvas layout may lag behind the new raw graph report

How to use this note:

1. read the curated root notes first
2. enter the generated symbol pages from the links below
3. use `graphify-out/GRAPH_REPORT.md` as the source of truth for current hubs and community counts
4. do not treat `graph.canvas` layout as authoritative

Fresh raw graph hubs:

- `parseManifestJson()` -> [[graphify-generated/parseManifestJson().md]]
- `ensureSnapshot_()` -> generated page was not found in the current export snapshot; use `graphify-out/GRAPH_REPORT.md` as raw truth
- `invalidateSnapshotCache_()` -> [[graphify-generated/invalidateSnapshotCache_().md]]
- `buildSnapshot()` -> [[graphify-generated/buildSnapshot().md]]
- `baseSnapshotWithLiveDataCache()` -> generated page was not found in the current export snapshot; use raw report first
- `paint()` -> [[graphify-generated/paint().md]]
- `updateHover_()` -> [[graphify-generated/updateHover_().md]]
- `abortCoordinatorBatch_()` -> [[graphify-generated/abortCoordinatorBatch_().md]]
- `registerLiveSources_()` -> generated page was not found in the current export snapshot; use raw report first
- `replayFailureText()` -> [[graphify-generated/replayFailureText().md]]

Product entrypoints:

- capture center: [[03_CAPTURE]], [[graphify-generated/CaptureCoordinator.md]], [[graphify-generated/CaptureCoordinator.cpp.md]], [[graphify-generated/CaptureCoordinatorRuntime.cpp.md]]
- manifest/session loading: [[CURRENT_REALITY_STATUS]], [[graphify-generated/SessionManifest.cpp.md]], [[graphify-generated/parseManifestJson().md]]
- replay center: [[04_REPLAY_VALIDATION]], [[graphify-generated/SessionReplay.md]], [[graphify-generated/SessionReplay.cpp.md]]
- viewer center: [[05_GUI_VIEWER]], [[17_VIEWER_BASELINE_2026_04]], [[graphify-generated/ChartController.md]], [[graphify-generated/ChartController.cpp.md]], [[graphify-generated/buildSnapshot().md]], [[graphify-generated/updateHover_().md]], [[graphify-generated/paint().md]]
- app/viewmodel seam: [[graphify-generated/AppViewModel.md]], [[graphify-generated/AppViewModel.cpp.md]]

Operational rule:

- use the root vault graph for curated notes only
- use generated symbol pages as drill-down pages, not as the primary map
- if a symbol is important in `GRAPH_REPORT.md` but missing here, the raw graph is newer than the generated export and the generated layer needs a dedicated re-export step
