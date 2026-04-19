# MCP Stack

Purpose:

- Fix which MCPs are useful for `hft-recorder`.
- Keep recorder agents from mixing workflow, retrieval, and vendor docs.

Chosen MCP stack:

- `Linear MCP` = recorder project/issues/comments/status
- `Qdrant MCP` = recorder semantic retrieval and compact memory
- `Context7 MCP` = current vendor/library docs
- `GitHub MCP` = repo/PR review context only if that workflow matters

Recommended priority:

1. `Qdrant MCP`
2. `Linear MCP`
3. `Context7 MCP`
4. `GitHub MCP` when needed

What each MCP is for:

- `Linear MCP`
  - backlog
  - blocked tasks
  - recorder execution status
- `Qdrant MCP`
  - runtime summaries
  - decisions/gotchas
  - integration summaries
- `Context7 MCP`
  - Qt
  - CMake/vendor libs
  - current third-party APIs
- `GitHub MCP`
  - PR context
  - review comments
  - issue discussion

What not to do:

- do not use `Linear MCP` as architecture memory
- do not use `Context7 MCP` for recorder-local truth
- do not use `Qdrant MCP` as a raw dump of full notes, full source, or recordings

Recorder rule:

- start with Obsidian
- use `Linear MCP` for task state
- use `Qdrant MCP` for filtered semantic recall
- use `Context7 MCP` only for external dependencies
