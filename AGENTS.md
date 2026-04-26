# Agent rules - hft-recorder (local nested repo)

This repository is a standalone application over `CXETCPP`.

## Hard safety stop - no Git / no remote automation

- The agent must not run any `git` command unless the user explicitly asks for that exact Git action in the current message.
- This ban includes read-only commands such as `git status`, `git diff`, `git log`, `git show`, `git ls-files`, `git grep`, and `git -C ...`.
- Reason: even read-only Git commands can produce CRLF/index warnings, touch Git internals through platform tooling, or trigger unwanted surrounding automation/noise.
- Use filesystem tools instead: `Get-ChildItem`, `rg`, `Get-Content`, targeted file reads, and direct path inspection.
- Do not start remote/compact/background automation tasks unless the user explicitly asks for them in the current message.
- Do not check GitHub, remotes, CI status, pull requests, releases, or other network-backed repository state unless the user explicitly asks for that exact remote/GitHub action in the current message.
- If repository state is needed, ask the user to run the Git command or paste the relevant output.

## Core contract

- `hft-recorder` is not part of the core `CXETCPP` library.
- It consumes `CXETCPP` as a prebuilt dependency:
  - shared library: for example `../build/libcxet_lib.so`
  - public headers: only the API surface required by the app
- Do not compile `CXETCPP` sources inside this repo.
- Do not use `add_subdirectory(..)` or vendor the parent library source tree here.
- Do not depend on `network/`, `parse/`, `exchanges/`, or other library internals.

## Build and test restraint

- Do not build or run tests automatically after every code edit.
- Build or test only when it is genuinely needed to validate a risky change,
  diagnose a concrete failure, or when the user explicitly asks for it.
- Prefer targeted checks over broad rebuilds when a narrow verification is enough.
- If a build is optional rather than necessary, explain the tradeoff and leave it
  to the user instead of starting it by default.

## Current truth

- Main product direction: `Qt 6 + QML` GUI-first application
- First milestone: capture normalized market data into canonical JSON corpus,
  replay it, validate it, and visualize it
- Compression research happens on top of that corpus
- The canonical corpus is more important right now than the old `.cxrec`-first plan

## Current work priorities

1. `src/core/capture/`
2. `src/core/corpus/`
3. `src/core/validation/`
4. `src/gui/`
5. `src/core/lab/`
6. `src/variants/`

## Stream semantics

- Never assume `trades == aggTrade`.
- The current Binance FAPI library path is semantically tied to `aggTrade`.
- Keep that distinction explicit in filenames, schema, and benchmark labels.

## Research priorities

- Baseline comparisons are mandatory:
  - `zstd`
  - `lz4`
  - `brotli`
  - `xz/lzma`
- Custom ideas are mandatory:
  - trade-specific
  - L1-specific
  - orderbook-specific
- Rankings must be per stream family, not global.

## GUI product rule

- The GUI is not a thin wrapper around CLI tools.
- The GUI is part of the deliverable:
  - capture
  - session browsing
  - validation
  - compression lab
  - results dashboard

## Karpathy-style behavioral rules

Source: https://github.com/forrestchang/andrej-karpathy-skills

These rules are secondary to the hft-recorder rules above. In particular, they do not relax the local bans on Git commands, remote automation, compiling CXETCPP sources inside this repo, or unnecessary builds/tests.

### 1. Think before coding

- Do not assume silently. State important assumptions before implementation.
- If multiple interpretations exist, surface them instead of picking one invisibly.
- If a simpler approach fits the request better, say so and explain the tradeoff.
- If the task is unclear enough that a reasonable implementation would be risky, stop and ask.

### 2. Simplicity first

- Write the minimum code that solves the requested problem.
- Do not add speculative features, one-off abstractions, or configurability that was not requested.
- Do not add error handling for impossible scenarios just to make code look defensive.
- If a solution is much larger than the problem requires, simplify it before presenting it.

### 3. Surgical changes

- Touch only files and lines that trace directly to the user's request.
- Do not improve adjacent code, comments, formatting, or structure as a drive-by change.
- Match existing style even when a different style would be personally preferred.
- Remove only unused code created by the current change. Mention unrelated dead code instead of deleting it.

### 4. Goal-driven execution

- Convert non-trivial tasks into verifiable success criteria.
- For bug fixes, prefer a reproducing test or targeted check before and after the fix.
- For refactors, preserve behavior and verify with the narrowest meaningful check.
- For multi-step work, keep a short plan with each step tied to a concrete verification.
