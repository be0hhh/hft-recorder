# hft-recorder

`hft-recorder` is a standalone desktop application and research lab on top of
`CXETCPP`.

Current truth:
- GUI-first product: `Qt 6 + QML`
- first milestone: capture clean normalized market data, replay it, validate it,
  and visualize it
- canonical session format: JSON corpus per session
- compression research happens on top of that corpus

Current Phase-1 capture target:
- fixed source: `Binance FAPI / ETHUSDT`
- one shared session folder under a user-selected parent directory
- three independent GUI buttons:
  - `Trades`
  - `BookTicker`
  - `Orderbook`
- orderbook contract:
  - first write `snapshot_000.json` from REST
  - then append WS deltas into `depth.jsonl`

User build/run entrypoints:
- `./compile.sh`
  - builds `CXETCPP` shared library if needed
  - installs it into `~/.local/cxet`
  - configures and builds `hft-recorder`
- `./compile.sh --force-cxet`
  - forcibly rebuilds and reinstalls `CXETCPP` before building `hft-recorder`
  - use this when public headers in `~/.local/cxet` became stale relative to the current repo
- `./build/start`
  - launches the Qt GUI with correct `LD_LIBRARY_PATH`

Primary user workflow:
1. Open the GUI.
2. Select exchange / market / symbols / duration.
3. Capture a session into normalized JSON files.
4. Open that session in validation and chart views.
5. Run baseline and custom compression pipelines in the lab.
6. Compare ratio, encode speed, decode speed, and lossless accuracy inside the GUI.

Dependency contract:
- `hft-recorder` does not compile `CXETCPP` sources
- `hft-recorder` links only against a prebuilt shared library such as `build/libcxet_lib.so`
- `hft-recorder` includes only the public API surface needed to consume `CXETCPP`
- no `add_subdirectory(..)` on the parent project
- no dependency on `network/`, `parse/`, `exchanges/`, or runtime internals

Repository layout:
- `doc/` - source-of-truth architecture, corpus, GUI, and lab docs
- `src/gui/` - Qt/QML app shell, models, and viewmodels
- `src/core/` - capture, corpus, validation, ranking, and lab backend
- `src/variants/` - custom compression candidates
- `src/support/` - wrappers around baseline compression libraries
- `scripts/python_lab/` - offline exploration and reports

Reading order:
1. `doc/OVERVIEW.md`
2. `doc/SESSION_CORPUS_FORMAT.md`
3. `doc/GUI_PRODUCT.md`
4. `doc/IMPLEMENTATION_PLAN.md`
5. `doc/ARCHITECTURE.md`
6. `doc/VALIDATION_AND_RANKING.md`
7. `doc/STREAMS.md`
8. `doc/CONFIG_AND_CLI.md`
9. `doc/TESTING_CONTRACT.md`
10. `doc/BUILD_AND_ISOLATION.md`
