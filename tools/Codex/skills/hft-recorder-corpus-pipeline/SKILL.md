---
name: hft-recorder-corpus-pipeline
description: Build and review hft-recorder capture, canonical corpus, replay, manifest, and validation workflows. Use for changes under recorder capture/corpus/validation code, session artifacts, logical stream semantics, or recorder-to-CXET data boundaries.
---

# HFT Recorder Corpus Pipeline

## Workflow

1. Read `apps/hft-recorder/AGENTS.md` and `apps/hft-recorder/docs/Agents/RoleRouter.md`.
2. Select `recorder_corpus_engineer`; add `architect` when touching a CXET public boundary, schema, shared artifact, or another app.
3. Inspect the actual capture-to-corpus-to-replay/validation path. Search `/mnt/d/recordings` before treating repo-local recordings as current.
4. Classify the logical stream precisely. Keep realtime `trade` distinct from historical/cold `aggTrade` in schema, files, manifests, and labels.
5. Preserve canonical corpus row contracts, ingest/sequence ordering, manifest counters, partial-session behavior, and deterministic replay.
6. Keep recorder requests logical. Leave exchange endpoint selection, wire routing, and message fanout in CXETCPP public contracts.
7. Implement only after explicit authorization. Give editing workers non-overlapping files and retain shared manifests or registries in the primary agent.
8. Run no Git, build, test, generated rewrite, runtime, or remote command unless the current message explicitly grants that exact action.

## Safety gates

- Reject silent schema, stream, ordering, replay, or validation fallbacks.
- Do not compile, vendor, or include CXETCPP internals in recorder.
- Do not infer missing events from a zero-byte file without checking the manifest and capture status.
- Preserve failure-visible handling for incomplete, corrupt, stale, and incompatible sessions.
- Report code-only conclusions as `static-only` until the named verification runs.

## Report

State corpus/schema impact, stream semantics, ordering, manifest behavior, replay/validation behavior, CXET boundary impact, changed files, risks, allowed manual verification commands, and what was not verified.
