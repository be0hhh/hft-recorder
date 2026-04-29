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
- `./compile.sh --force clang`
  - rebuilds hft-compressor, CXETCPP, and hft-recorder with Clang, replacing the active `~/.local/cxet` install and `build/` app binaries
- `./compile.sh --force gcc`
  - same flow with GCC, intended for back-to-back metric comparisons
- `./build/start`
  - launches the Qt GUI in CPU-safe software mode
- `./build/start --gpu`
  - launches the Qt GUI requesting the Qt Quick OpenGL backend
  - intended for real Linux desktop GPU validation, e.g. Ubuntu 24 on the laptop

Current runtime truth:
- default runtime is intentionally software-safe
- `--gpu` enables a hardware-backed Qt Quick/OpenGL setup for validation
- the active viewer path is still `ChartItem` (`QQuickPaintedItem`)
- this means current `--gpu` is hardware-backed compositing, not yet a separate GPU-native chart renderer
- `./build/start` exports `HFTREC_METRICS_PORT=8080` and `HFTREC_METRICS_MODE=full` by default; `/metrics` includes `hftrec_build_info{compiler="..."}` so Prometheus/Grafana can separate Clang and GCC runs

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

Public contract freeze:
- recorder-visible event meaning for trades, bookticker, and orderbook must stay stable
- `TradePublic`, `BookTickerData`, and `OrderBookSnapshot` are the compatibility/output contracts
- internal runtime payloads may change in memory/layout, but only behind the explicit compatibility bridge
- `hft-recorder` is allowed to consume `TradeRuntimeV1` / `BookTickerRuntimeV1` only at the capture boundary where they are immediately materialized into stable recorder rows
- `hft-recorder` must not depend on internal parser/layout details beyond that bridge

Laptop transfer note:
- code moves by normal git branch workflow
- `recordings/` is intentionally ignored and should be transferred separately from the repo
- if a ready `libcxet_lib.so` and matching public headers are already available, copy the installed `~/.local/cxet/` tree to the laptop before running `./compile.sh`
- if that copied install is incompatible on Ubuntu 24, rerun `./compile.sh --force-cxet` on the laptop to rebuild and reinstall `CXETCPP`

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
