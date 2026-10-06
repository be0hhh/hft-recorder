# hft-recorder

[![corpus-contract](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml/badge.svg?branch=main)](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml?query=branch%3Amain)

hft-recorder is the GUI-first capture, corpus, validation and compression-lab
product in the CXET source family.

## Current product contract

- Qt 6 + QML desktop workflow;
- Parser-backed realtime BBO, trades and depth capture;
- sealed sharded compressed binary corpus, decoded directly into RAM;
- replay, validation and charts over canonical corpus data;
- baseline and custom compression experiments over the same corpus;
- active WSL recordings under "/mnt/d/recordings" when that directory exists.

Recorder owns captured corpus files. It does not own exchange endpoint/wire
grammar, Trader execution, Backtest fills or Parser public topology.

## Source and build boundary

The CXET root is the only canonical family build graph. Recorder compiles as a
direct source target and consumes the already-defined public "cxet::cxet_lib",
Parser producer-client, compressor and corpus-contract targets.

Recorder does not:

- compile CXET implementation sources;
- include CXET "network/", "parse/" or "exchanges/" internals;
- copy or stage a sibling SDK;
- import a sibling shared library as a family fallback;
- download a missing family dependency.

A standalone Recorder configure may fail with a clear message when the family
graph is absent.

## Capture modes

### Realtime capture

The GUI and `capture` CLI manage one Parser process for the complete selected
batch. The secret-free generated INI is private and adjacent to the selected
template's existing `.env`; credentials are not copied. Parser owns native
subscriptions, normalization and receive clocks. Historical candle capture
remains a separate cold action.

A capture has explicit duration and physical byte limits;
the first limit reached stops admission, drains committed records within the
reserved final budget, imports loss evidence and seals the corpus.
Automatic duration/quota/error termination keeps the owned Parser running.
Stop the session explicitly to close that producer and its private launch
resources before another capture; closing Recorder also stops only its child.

Records preserve exchange timestamps and Parser application arrival in realtime
and monotonic nanoseconds. Bounded channel blocks use the Compressor's lossless
trade/BBO/depth transforms and entropy coding; recording creates no raw spool or
JSON intermediate. Replay decodes each checked block directly into bounded RAM.

The arrival boundary is the accepted complete application message, rather than
kernel/NIC receive time. Corpus schema 7 and record schema 6 also retain native
quantity rules separately from the canonical executable E8 grid proven by the
selected connector. Per-channel quantity authority and original native factors
let Backtest project Spot and linear Futures/Swap volumes exactly while keeping
the recorded payload lossless. Missing market or execution authority stays explicit.

Dynamic selection commands go to the Parser owner. Applied transitions are
recorded as administrative markers in the same shard stream. Unsupported native
routes are rejected explicitly. Capture loss remains an exact-replay blocker.
Current dynamic changes require isolated WS BBO/trade lanes. New-source append
requires an already active product in standalone one-shard capture; shared,
native-all-market and depth mutations, and an entirely empty startup product,
are refused. Shared-lane health markers carry their bounded application-drain
time; they do not certify the original kernel closing time.

The `parser-capture` CLI can attach to an already running same-UID Parser.

## User workflow

1. Choose an output directory, normally below "/mnt/d/recordings".
2. Select exchange, market, symbols, logical streams and limits.
3. Capture a session.
4. Stop and finalize the recording.
5. Open its folder in Backtests; choose an instrument or the whole eligible
   market and use the canonical strategy configuration.
6. Compression measurements remain a separate offline research step. No live
   market capacity or four-core/four-GB throughput is established by source edits.

## Repository map

- "src/Runtime/include/hftrec/" — Recorder public API;
- "src/Runtime/src/Capture/" — capture lifecycle and normalized sinks;
- "src/Runtime/src/corpus/" — canonical corpus readers/writers;
- "src/Runtime/src/Validation/" — integrity and semantic validation;
- "src/Lab/src/" — compression experiments and reports;
- "src/Gui/src/" — Qt/QML product;
- "src/Contracts/" — directly compiled shared corpus contract;
- "docs/" — current product documentation; "doc/" retains historical plans and research.

Start with [docs/README.md](docs/README.md).

## CI coverage

The `corpus-contract` workflow overlays the triggering Recorder revision into
the private CXETCPP direct-source family graph. Its retired Backtest
session-loader test target provides no current corpus acceptance evidence. The
registered owner offline suites cover current corpus and capture contracts;
actual Qt/QML actions require separate acceptance. The badge is a workflow link,
not evidence of current passing CI or Qt/QML product acceptance.

The root checkout requires the repository secret `CI_CXETCPP_SSH_KEY`. Fork
pull requests without that secret fail explicitly before checkout and do not
provide a passing corpus-contract result.

## Build

Prepare Qt 6 development packages separately. The build reuses
their CMake targets and fails if a required package is absent; it does not fetch them.

From this directory in the canonical CXET checkout:

```bash
./compile.sh p
./compile.sh --force portable p
./compile.sh all p
./compile.sh all portable p
```

The default builds this owner's product incrementally with pinned Clang, GNU
Make, Release `-O3`, native CPU targeting and LTO OFF. `portable` disables CPU
targeting. Fresh foreign providers are reused silently; missing or stale foreign
modules are listed in one combined prompt before rebuilding them. Decline or EOF
cancels; a noninteractive invocation with stale providers exits with an explanatory
error. `--force` builds the selected product and necessary closure incrementally
with FULL LTO, without cleaning or prompting; ThinLTO is never selected.

`all` builds and runs only this owner's registered tests and needed dependencies.
`--force all` first builds the optimized product, then local tests. Unrelated
products, benchmarks and other owners' tests are not selected. An empty test
registry is reported explicitly and does not establish passing test proof.

Optimized trees use `build` (native) or `build/modes/portable`; development trees
use `build/modes/dev-native` or `build/modes/dev-portable`. `CXET_BUILD_DIR` is the
exact caller-supplied path; an incompatible existing profile is rejected. Only a
successful product build updates `build/.compile-active/<owner>.json`; default
launchers resolve that selected tree. Failed, UI-only and test-only runs do not
switch it. Project-owned libraries remain static `.a`; `p` selects available
processors and `-j N` overrides it. See `--help` for supported options.
