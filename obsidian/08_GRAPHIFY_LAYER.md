# Graphify Layer

Purpose:

- Keep a machine-generated symbol graph next to the curated agent notes.
- Let agents drill down from module notes into symbol-level graph when needed.

Generated corpus summary:

- `graphify` run date: `2026-04-19`
- scope: `apps/hft-recorder`
- mode: local AST-only for code graph
- detected corpus: 183 files total, 127 code files, 56 docs
- AST extraction scope: 134 code/QML files
- extracted code graph: 453 nodes, 514 edges

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
- do not overwrite curated root notes with generated output
