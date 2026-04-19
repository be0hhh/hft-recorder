# Decisions

Purpose:

- Store compact architecture and tooling decisions that agents should reuse.

Current decisions:

- `obsidian is the primary project context layer`
  - status: `active`
  - reason: architecture, tasks, decisions, and gotchas stay in one local-first place

- `generated graph is secondary navigation`
  - status: `active`
  - reason: symbol graph helps drill down but does not explain product truth

- `linear is the workflow layer`
  - status: `active`
  - reason: good for backlog, statuses, and issue dependencies

- `qdrant is the retrieval layer`
  - status: `active`
  - reason: good fit for compact memory plus semantic retrieval without replacing local notes

- `mcp layer is split by function`
  - status: `active`
  - reason: workflow, retrieval, and external docs need different connectors

- `vector memory must stay compressed`
  - status: `active`
  - reason: store facts/constraints/preferences, not long raw documents

- `external docs grounding is for vendor APIs only`
  - status: `active`
  - reason: local repo truth should come from code plus curated notes
