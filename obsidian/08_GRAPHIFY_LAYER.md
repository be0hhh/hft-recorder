# Graphify Layer

Purpose:

- Keep a machine-generated symbol graph next to the curated agent notes.
- Let agents drill down from module notes into symbol-level graph when needed.

Generated corpus summary:

- `graphify` raw graph refresh date: `2026-04-22`
- scope: `apps/hft-recorder`
- mode: local AST-only for code graph
- detected corpus: 176 files total, ~92,175 words
- AST extraction scope: 176 files
- extracted code graph: 927 nodes, 1247 edges
- detected communities: 159

Locations:

- raw graph outputs: `graphify-out/`
- generated Obsidian vault: `graphify-generated/`
- canvas: `graphify-generated/graph.canvas`

What the generated layer is good at:

- symbol-level neighbors
- file to function links
- quick local graph hopping

What it is not good at:

- product-level explanation
- distinguishing active code from scaffold
- understanding docs semantics in this run

Freshness status:

- `graphify-out/` was refreshed on `2026-04-22`
- `graphify-generated/` in this workspace still has `2026-04-21` timestamps
- treat generated symbol pages and `graph.canvas` as potentially stale until the Obsidian export step is rerun from the refreshed `graphify-out/graph.json`

How to use it:

1. read the curated notes in this vault first
2. if you need symbol detail, enter `graphify-generated/`
3. use `graph.canvas` or the generated `_COMMUNITY_*` pages only as secondary navigation

Regeneration intent:

- keep `.graphifyignore` authoritative
- regenerate raw graph when code shape changes materially
- regenerate `graphify-generated/` from the refreshed `graphify-out/graph.json` when symbol navigation is expected to stay aligned with the current code
- do not overwrite curated root notes with generated output
- current raw graph was refreshed on `2026-04-22`
- current generated vault snapshot in this workspace is still from `2026-04-21`
