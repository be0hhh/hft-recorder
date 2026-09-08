# Agent rules - hft-recorder

`hft-recorder` is a standalone GUI-first application and research lab over CXET.

## Project-owned versioning

- Keep one canonical current recorder contract. Do not add parallel
  `V1`/`V2`/`V3` types, compatibility aliases or forwarding facades; update all
  in-repo producers and consumers atomically.
- Keep numeric corpus/storage/ABI guards and fail closed on mismatch, but do not
  put `V<number>` in current recorder-owned type, function, constant, file or
  directory names.
- Old corpus/session formats may retain versioned names only inside isolated
  read-only migration code. The current writer is unique; external protocol
  tokens and immutable historical evidence remain unchanged.

## Hard safety stop - no Git / no remote automation

- Do not run any Git command in this repository unless the user explicitly asks for that exact Git action in the current message.
- This ban includes read-only commands such as `git status`, `git diff`, `git log`, `git show`, `git ls-files`, `git grep`, and `git -C ...`.
- Use filesystem tools instead: `rg`, targeted file reads, direct path inspection.
- Do not check GitHub, remotes, CI status, pull requests, releases, or other network-backed repository state unless the user explicitly asks for that exact remote/GitHub action.
- If repository state is needed, ask the user to run the Git command or paste the relevant output.

## Core contract

- `hft-recorder` is not part of the core CXET library.
- In the CXET superproject it consumes the already-defined public
  `cxet::cxet_lib`, Parser producer-client, compressor and corpus-contract
  targets through direct target edges.
- A standalone Recorder configure may fail with a clear message when the CXET
  family graph is absent; it must not download, vendor, copy or import sibling
  family artifacts as a fallback.
- Do not compile CXET implementation sources inside Recorder or depend on CXET
  `network/`, `parse/`, `exchanges/`, or other library internals.

## Current truth

- Main product direction: Qt 6 + QML GUI-first application.
- First milestone: capture normalized market data into canonical JSON corpus, replay it, validate it, and visualize it.
- Compression research happens on top of that corpus.
- The canonical corpus is more important right now than the old `.cxrec`-first plan.
- Current user recordings live on the Windows `D:` drive. From WSL, inspect `/mnt/d` first, especially `/mnt/d/recordings` when it exists, before assuming `apps/hft-recorder/recordings` contains the active corpus.

## Current work priorities

1. `src/Runtime/src/Capture/`
2. `src/Runtime/src/corpus/`
3. `src/Runtime/src/Validation/`
4. `src/Gui/src/`
5. `src/Lab/src/`
6. `src/Lab/Variants/`

## Stream semantics

- Never assume live `trade` equals historical/cold `aggTrade`.
- Binance FAPI realtime trade capture is a separate logical live stream; historical warmup may use `aggTrade` when that is the configured history source.
- Keep that distinction explicit in filenames, schema, and benchmark labels.
- Recorder UI/CLI asks for logical streams only. CXET must choose the real exchange route and fan out one wire message into all requested logical streams when needed.
- Do not hardcode exchange-specific wire routing in recorder.

## Research priorities

- Baseline comparisons are mandatory: `zstd`, `lz4`, `brotli`, `xz/lzma`.
- Custom ideas are mandatory for trade-specific, L1-specific, and orderbook-specific streams.
- Rankings must be per stream family, not global.

## GUI product rule

The GUI is part of the deliverable, not a thin CLI wrapper:

- capture;
- session browsing;
- validation;
- compression lab;
- results dashboard.

## Build and test restraint

- Do not compile, build, or run tests/checks without the user's explicit consent in the current message.
- Prefer targeted checks over broad rebuilds when a narrow verification is enough.
- If a build is optional rather than necessary, explain the tradeoff and leave it to the user.

## Subagents

Use GPT-5.6-sol high without fast mode for recorder subagents. GPT-5.6-terra high without fast mode is allowed for easy tasks. Use xhigh/max only when the user explicitly requests xhigh/max in the current message.

Read-only explorer and reviewer subagents may be used automatically for non-trivial recorder work. Editing worker subagents may be used automatically only after the current user message explicitly authorizes implementation, for example with `делай`, `implement`, or an equivalent direct instruction. Read-only requests such as `изучи`, `посмотри`, or `пока не делай` never authorize editing workers.

Allowed scopes: capture, corpus, validation, GUI, lab, compression variants, documentation.

Use at most three recorder workers concurrently. Give each worker a concrete, non-overlapping file scope; keep shared integration, registry, and manifest files with the primary agent. Stop and ask the user if ownership overlaps or the implementation has no single safe design.

Every subagent must read this file before acting. Subagents must not run Git commands, GitHub/remote checks, CI/release inspection, builds, tests, compiles, generated-file rewrites, runtime binaries, or long-running processes unless the current user message explicitly grants that exact action. Implementation authorization alone does not grant any of those actions.
