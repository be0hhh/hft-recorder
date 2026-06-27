# Agent rules - hft-recorder

`hft-recorder` is a standalone GUI-first application and research lab over CXETCPP.

## Hard safety stop - no Git / no remote automation

- Do not run any Git command in this repository unless the user explicitly asks for that exact Git action in the current message.
- This ban includes read-only commands such as `git status`, `git diff`, `git log`, `git show`, `git ls-files`, `git grep`, and `git -C ...`.
- Use filesystem tools instead: `rg`, targeted file reads, direct path inspection.
- Do not check GitHub, remotes, CI status, pull requests, releases, or other network-backed repository state unless the user explicitly asks for that exact remote/GitHub action.
- If repository state is needed, ask the user to run the Git command or paste the relevant output.

## Core contract

- `hft-recorder` is not part of the core CXETCPP library.
- It consumes CXETCPP as a prebuilt dependency: shared library plus public headers.
- Do not compile CXETCPP sources inside this repo.
- Do not use `add_subdirectory(..)` or vendor the parent library source tree here.
- Do not depend on CXETCPP `network/`, `parse/`, `exchanges/`, or other library internals.

## Current truth

- Main product direction: Qt 6 + QML GUI-first application.
- First milestone: capture normalized market data into canonical JSON corpus, replay it, validate it, and visualize it.
- Compression research happens on top of that corpus.
- The canonical corpus is more important right now than the old `.cxrec`-first plan.
- Current user recordings live on the Windows `D:` drive. From WSL, inspect `/mnt/d` first, especially `/mnt/d/recordings` when it exists, before assuming `apps/hft-recorder/recordings` contains the active corpus.

## Current work priorities

1. `src/core/capture/`
2. `src/core/corpus/`
3. `src/core/validation/`
4. `src/gui/`
5. `src/core/lab/`
6. `src/variants/`

## Stream semantics

- Never assume live `trade` equals historical/cold `aggTrade`.
- Binance FAPI realtime trade capture is a separate logical live stream; historical warmup may use `aggTrade` when that is the configured history source.
- Keep that distinction explicit in filenames, schema, and benchmark labels.
- Recorder UI/CLI asks for logical streams only. CXETCPP must choose the real exchange route and fan out one wire message into all requested logical streams when needed.
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

Subagents may be used for local file inspection, planning, and disjoint module edits only after the user approves subagent use for the current task.

Allowed scopes: capture, corpus, validation, GUI, lab, compression variants, documentation.

Subagents must not run Git commands, GitHub/remote checks, CI/release inspection, builds, tests, compiles, generated-file rewrites, or long-running binaries unless explicitly allowed.
