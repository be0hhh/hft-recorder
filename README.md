# hft-recorder

`hft-recorder` is a standalone application on top of `CXETCPP`.

Current scope:
- build a custom, highly specialized market-data compression library
- research many custom ideas for:
  - `trades / aggTrade`
  - `L1 / bookTicker`
  - `orderbook updates`
- compare those ideas against standard codecs and hybrid baselines
- use Python for fast prototyping and offline analysis
- move the strongest winners into a C++ core library
- define an online compression path that works during live recording

Dependency contract:
- `hft-recorder` does not compile `CXETCPP` sources
- `hft-recorder` links only against a prebuilt shared library such as `build/libcxet_lib.so`
- `hft-recorder` includes only the public API surface and public data types needed to consume the library
- no `add_subdirectory(..)` on the parent `CXETCPP` project
- no direct dependency on `network/`, `parse/`, `exchanges/`, or runtime internals

Repository layout:
- `doc/` - research and design docs
- `src/` - future application and C++ core code
- `include/` - future local public headers for the custom compression library
- `tests/` - recorder/core tests
- `bench/` - benchmark harness
- `scripts/` - helper scripts and dataset tooling

Reading order:
1. `doc/OVERVIEW.md`
2. `doc/RESEARCH_PROGRAM.md`
3. `doc/CUSTOM_IDEA_CATALOG.md`
4. `doc/COMPARISON_MATRIX.md`
5. `doc/DATASET_CAPTURE_PROTOCOL.md`
6. `doc/ORDERBOOK_REPRESENTATION_EXPERIMENTS.md`
7. `doc/IMPLEMENTATION_PLAN.md`
8. `doc/SOURCE_LAYOUT_AND_VARIANTS.md`
9. `doc/BENCHMARK_PLAN.md`
10. `doc/CODEC_VARIANTS.md`
11. `doc/STREAMS.md`
12. `doc/ARCHITECTURE.md`
13. `doc/IMPLEMENTATION_NOTES.md`
14. `doc/FILE_FORMAT.md`
15. `doc/BUILD_AND_ISOLATION.md`
