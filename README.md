# hft-recorder

[![corpus-contract](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml/badge.svg?branch=main)](https://github.com/be0hhh/hft-recorder/actions/workflows/cpp.yml?query=branch%3Amain)

hft-recorder is the GUI-first capture, corpus, validation and compression-lab
product in the CXET source family.

## Current product contract

- Qt 6 + QML desktop workflow;
- normalized JSON session corpus for direct Recorder capture;
- sealed sharded binary corpus for Parser-wide capture;
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

### Direct GUI capture

The first direct capture milestone records normalized Binance FAPI streams into
one user-selected session directory:

- Trades;
- BookTicker;
- Depth deltas from the native stream.

Direct capture uses the shared CXET Core `MarketRuntime`. The current canonical
JSON writer cannot represent depth snapshot/rebase metadata or Trade
`Unknown` initiator side: those inputs stop recording with an explicit error
and loss evidence. It does not seed the JSON corpus with an initial snapshot.

The session JSON files are the canonical input for validation, charts and the
compression lab.

### Parser-wide capture

Recorder attaches to Parser's same-UID capture contract. It does not launch,
replace or configure parserd. A capture has explicit duration and byte limits;
the first limit reached stops admission, drains committed records within the
reserved final budget, imports loss evidence and seals the corpus.

Backtest selection over the sealed corpus is explicit by source, receive-time
range and strategy-required channels. Missing, stale, degraded, gapped or
generation-crossing input fails closed.

## User workflow

1. Choose an output directory, normally below "/mnt/d/recordings".
2. Select exchange, market, symbols, logical streams and limits.
3. Capture a session.
4. Validate the corpus and inspect charts.
5. Run baseline and custom compression pipelines.
6. Compare ratio, encode/decode speed and lossless verification per stream
   family.

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
session-loader test target provides no current corpus acceptance evidence. New
meaningful corpus and GUI checks are deferred. The badge is a workflow link,
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
