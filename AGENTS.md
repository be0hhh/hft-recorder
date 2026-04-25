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
