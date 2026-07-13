---
name: hft-recorder-compression-lab
description: Build and review hft-recorder compression-lab pipelines and reports over canonical recorder corpus data. Use for codec experiments, baseline comparisons, lossless verification, per-stream rankings, compression artifacts, or GUI lab integration.
---

# HFT Recorder Compression Lab

## Workflow

1. Read `apps/hft-recorder/AGENTS.md` and `apps/hft-recorder/doc/agents/ROLE_ROUTER.md`.
2. Select `compression_engineer`; add `recorder_corpus_engineer` for corpus semantics and `architect` for container, public, or cross-app boundaries.
3. Identify corpus identity, stream family, schema/version, block size, and the exact encode/decode boundary before comparing pipelines.
4. Compare mandatory baselines: `zstd`, `lz4`, `brotli`, and `xz/lzma`.
5. Evaluate custom trade, L1, and orderbook pipelines separately. Rank within each stream family, never globally across unrelated data.
6. Record ratio, encode throughput, decode throughput, memory/bounds, corpus identity, pipeline identity, and availability reason.
7. Require byte- or row-equivalent lossless decode verification for every lossless result. Exercise partial, corrupt, truncated, and incompatible artifact behavior when verification is authorized.
8. Keep experimental codecs in recorder lab or compressor-owned code; do not move them into CXETCPP core.
9. Implement only after explicit authorization and give workers non-overlapping files.
10. Run no Git, benchmark, build, test, generated rewrite, runtime, or remote command unless the current message explicitly grants that exact action.

## Safety gates

- Reject improvement claims without mandatory baselines and matching corpus identity.
- Reject available/ready status without functioning encode, decode, inspect, and lossless verification paths.
- Preserve pipeline/container metadata and compatibility, or require an explicit migration decision.
- Surface optional-library absence and decode corruption honestly.
- Report unexecuted measurements as planned, never observed.

## Report

State corpus and stream family, pipeline identity, mandatory baselines, lossless proof, ratio/speed evidence, compatibility/corruption behavior, changed files, risks, manual verification commands, and what was not verified.
