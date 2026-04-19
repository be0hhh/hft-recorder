# Agent Stack

Purpose:

- Define the recommended local-first stack around `hft-recorder`.
- Separate architecture truth, task tracking, memory, and third-party docs.

Recommended stack:

- Obsidian vault = primary project context and task layer.
- `Linear` = workflow and project tracking.
- `Qdrant` = retrieval layer for compact memory and semantic search.
- MCP layer = agent access to workflow, retrieval, and vendor docs.
- External docs layer = on-demand current vendor/library docs only.

What each layer should own:

- Obsidian:
  - architecture notes
  - file role map
  - active/backlog tasks
  - decisions
  - gotchas
- Memory layer:
  - user preferences
  - stable project constraints
  - short decisions worth carrying across sessions
  - recurring traps
- External docs layer:
  - Qt
  - CMake/package docs
  - Python/frontend libs
  - other fast-moving dependencies

Why not make `Linear` the only layer:

- it is a workflow/task product, not a code-context memory layer
- it does not replace architecture notes or semantic retrieval

Practical recommendation:

- keep using this Obsidian vault as the primary source of truth
- use `Linear` for backlog/status/dependencies
- use `Qdrant` for compressed facts and semantic recall
- if doc-grounding is added later, use it only for third-party APIs

Desired tooling vs session reality:

- `Linear`, `Qdrant`, `Context7`, and similar tooling are desired helpers, not guaranteed session truth
- the safe fallback order is:
  1. Obsidian curated notes
  2. nested `doc/`
  3. code
  4. external tooling only if actually available in the session
- do not spend early time assuming connectors exist just because the stack note names them

Token discipline:

- stable instructions first
- variable task input last
- inject note slices, not whole vault dumps
- prefer task/decision/gotcha notes over repeating long chat history

Retrieval rule:

- keep whole-note truth in Obsidian
- keep workflow state in `Linear`
- keep only compact summaries and durable facts in `Qdrant`
- [[15_VECTOR_RETRIEVAL]] = concrete retrieval policy
- [[16_MCP_STACK]] = concrete MCP usage policy
