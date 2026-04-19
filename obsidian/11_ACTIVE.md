# Active

Purpose:

- Hold only in-progress or immediately next tasks.

Current active tasks:

- `obsidian vault maintenance`
  - owner: `obsidian`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/00_INDEX.md`, `obsidian/CURRENT_REALITY_STATUS.md`
  - notes: keep curated layer compact; keep generated layer secondary

- `capture path truth audit`
  - owner: `capture`
  - status: `queued-active`
  - last_verified_at: `2026-04-19`
  - verified_against: `src/core/capture/CaptureCoordinator.cpp`
  - notes: re-check current code against `03_CAPTURE` when capture work resumes

- `replay/viewer truth audit`
  - owner: `replay/gui`
  - status: `queued-active`
  - last_verified_at: `2026-04-19`
  - verified_against: `src/core/replay/SessionReplay.cpp`, `src/gui/viewer/ChartController.cpp`
  - notes: viewer is already materially usable; re-check `SessionReplay`, rendering assumptions, and comparison workflow before the next major GUI pass

- `viewer baseline stabilization`
  - owner: `replay/gui`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `src/gui/viewer/ChartController.cpp`, `src/gui/qml/views/ViewerView.qml`, `src/gui/viewer/renderers/BookRenderer.cpp`
  - notes: current viewer now serves as the baseline comparison workbench; keep controls meaningful and preserve readability-first orderbook rendering

- `qdrant recorder retrieval scope`
  - owner: `agent tooling`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/15_VECTOR_RETRIEVAL.md`
  - notes: keep recorder collections narrow and integration-aware

- `recorder mcp usage policy`
  - owner: `agent tooling`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/16_MCP_STACK.md`
  - notes: fix which MCP to use for workflow, retrieval, and external docs

- `review backlog handoff`
  - owner: `review`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/REVIEW_BACKLOG.md`
  - notes: keep urgent implementation queue severity-ordered and handoff-ready
