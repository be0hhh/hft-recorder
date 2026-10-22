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

## Git and remote actions

- Read-only Git inspection is allowed when useful to the active task, including
  `git status`, `git diff`, `git log`, `git show`, `git ls-files` and `git grep`.
- Every Git write requires separate explicit authorization for the active task.
- Do not check GitHub, remotes, CI status, pull requests, releases, or other network-backed repository state unless the user explicitly asks for that exact remote/GitHub action.

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

- Do not compile, build, or run tests/checks without the user's explicit consent for the active task.
- Prefer targeted checks over broad rebuilds when a narrow verification is enough.
- If a build is optional rather than necessary, explain the tradeoff and leave it to the user.

## Subagents

Follow the canonical GPT-6.1 Sol model/effort routing in
`../../docs/Agents/Subagents.md`; do not keep an app-local model matrix.
Follow the same canonical owner for service-tier selection: standard/default
by default; Fast mode may be selected manually by the user.

Read-only explorer and reviewer subagents may be used automatically for non-trivial recorder work. Editing worker subagents may be used automatically only with explicit implementation authorization for the active task, for example with `делай`, `implement`, or an equivalent direct instruction. Read-only requests such as `изучи`, `посмотри`, or `пока не делай` never authorize editing workers.

Allowed scopes: capture, corpus, validation, GUI, lab, compression variants, documentation.

Follow `../../docs/Agents/Subagents.md` for useful delegation and configured
concurrency. Give each worker a concrete, non-overlapping file scope; keep shared
integration, registry, and manifest files with the primary agent. Stop and ask
the user if ownership overlaps or the implementation has no single safe design.

Every subagent must read this file before acting. Read-only Git inspection is
allowed. Git writes, GitHub/remote checks, CI/release inspection, builds, tests,
compiles, generated-file rewrites, runtime binaries and long-running processes
need their own active-task authorization. Check classification, command-package
approval and task-scoped repeats follow `../../docs/Agents/DevelopmentWorkflow.md`.
Implementation authorization alone does not grant those actions.

## Development workflow

Follow `../../docs/Agents/DevelopmentWorkflow.md` for useful regression tests,
optional test-first and user-selected check commands. Finish requested edits
independently of unrequested runs; implementation, CI, runtime and live evidence
remain separate.
