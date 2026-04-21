# Graphify Layer

Purpose:

- Keep a machine-generated symbol graph next to the curated agent notes.
- Let agents drill down from module notes into symbol-level graph when needed.

Generated corpus summary:

- `graphify` run date: `2026-04-21`
- scope: `apps/hft-recorder`
- mode: local AST-only for code graph
- detected corpus: 155 files total, ~75,855 words
- AST extraction scope: 155 files
- extracted code graph: 663 nodes, 809 edges

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

How to use it:

1. read the curated notes in this vault first
2. if you need symbol detail, enter `graphify-generated/`
3. use `graph.canvas` or the generated `_COMMUNITY_*` pages only as secondary navigation

Regeneration intent:

- keep `.graphifyignore` authoritative
- regenerate raw graph when code shape changes materially
- regenerate `graphify-generated/` from the refreshed `graphify-out/graph.json` when symbol navigation is expected to stay aligned with the current code
- do not overwrite curated root notes with generated output
- current raw graph and generated vault were refreshed on `2026-04-21`
