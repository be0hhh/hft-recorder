# Decisions

Purpose:

- Store compact architecture and tooling decisions that agents should reuse.

Current decisions:

- `obsidian is the primary project context layer`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/START_HERE_TRUTH_ORDER.md`
  - reason: architecture, tasks, decisions, and gotchas stay in one local-first place

- `generated graph is secondary navigation`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/08_GRAPHIFY_LAYER.md`
  - reason: symbol graph helps drill down but does not explain product truth

- `linear is the workflow layer`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/09_AGENT_STACK.md`
  - reason: good for backlog, statuses, and issue dependencies

- `qdrant is the retrieval layer`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/15_VECTOR_RETRIEVAL.md`
  - reason: good fit for compact memory plus semantic retrieval without replacing local notes

- `mcp layer is split by function`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/16_MCP_STACK.md`
  - reason: workflow, retrieval, and external docs need different connectors

- `vector memory must stay compressed`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/15_VECTOR_RETRIEVAL.md`
  - reason: store facts/constraints/preferences, not long raw documents

- `external docs grounding is for vendor APIs only`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/HISTORICAL_VS_ACTIVE.md`
  - reason: local repo truth should come from code plus curated notes

- `truth comes before breadth`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/CURRENT_REALITY_STATUS.md`
  - reason: subsystem maturity is uneven, so agents must know what is real before planning broad changes

- `metrics v1 before Grafana v2`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/OBSERVABILITY_PLAN.md`
  - reason: current implementation does not justify a full observability stack yet

- `viewer is now the comparison baseline`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `obsidian/17_VIEWER_BASELINE_2026_04.md`
  - reason: current app direction is visual and analytical comparison of original vs transformed or restored corpus outputs

- `orderbook controls are dollar-based`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `src/gui/qml/views/ViewerView.qml`, `src/gui/viewer/renderers/BookRenderer.cpp`
  - reason: percent-like controls were not meaningful for real orderbook inspection; user-facing controls now map to notional visibility and full-bright thresholds

- `viewer control state is persistent`
  - status: `active`
  - last_verified_at: `2026-04-19`
  - verified_against: `src/gui/viewmodels/AppViewModel.cpp`
  - reason: repeated manual reconfiguration wastes time and slows comparison work
