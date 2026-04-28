# hft-recorder - compressed replay flow

## Current implemented state

`hft-recorder` records the canonical session corpus as JSON files:

```text
manifest.json
trades.jsonl
bookticker.jsonl
depth.jsonl
snapshot_*.json
```

The `Compress` tab is the only GUI surface that talks to the standalone
`hft-compressor` shared library. The library is built separately in
`apps/hft-compressor` and imported by CMake as a prebuilt dependency. Recorder
does not build compressor as a child project.

The first working pipeline is:

```text
std.zstd_jsonl_blocks_v1
```

It reads one canonical JSONL channel and writes:

```text
compressedData/zstd/sessions/<session>/<channel>.hfc
compressedData/zstd/sessions/<session>/<channel>.metrics.json
```

Other pipelines shown by the tab are research placeholders. They are visible so
the coursework has stable names for standard baselines, Python prototypes,
domain transforms, hybrid pipelines, arithmetic coding, range coding, and rANS.

## Intended large-session workflow

Example target workflow:

1. User captures 5 hours of market data through `hft-recorder`.
2. The canonical JSONL session weighs about 10 GB.
3. User runs `Compress` and creates `.hfc` artifacts weighing about 3 GB.
4. A future replay/backtest loader opens the compressed artifacts.
5. The compressed bytes may be kept in memory or memory-mapped as compressed
   storage, but decoded JSON/rows must not be materialized as a full 10 GB copy.
6. The decoder emits blocks/rows on demand.
7. Recorder converts emitted rows into normalized replay events.
8. Viewer, backtest, and algo-facing adapters consume those events as a stream.

The key memory rule is that the 3 GB compressed representation may be resident,
but the 10 GB decoded corpus must not be expanded into RAM. Decoding must be a
streaming operation.

This rule applies to both viewer replay and algo-facing replay. A local exchange
run may keep compressed bytes or memory-mapped compressed blocks available, but
it must emit decoded rows/events incrementally through the replay clock.

## Future architecture seam

The next storage seam should be a backend-neutral session reader above file
layout details:

```text
compressed .hfc blocks
  -> hft-compressor decodeHfcBuffer / future streaming file decoder
  -> JSONL row chunks
  -> recorder JSON row parsers
  -> recorder-owned replay rows
  -> replay/event stream provider
```

That provider should satisfy the same event semantics as the current JSON
`SessionReplay`, but without requiring full historical materialization.

For algo-facing replay, this provider is not enough by itself. Rows must pass
through the local exchange fanout contract in
`REPLAY_TO_CXETCPP_FANOUT.md`, which merges channel rows into timestamp buckets
before CXETCPP delivers them to subscribers.

For the viewer this means visible-window reads and cursor seeks should be backed
by an on-demand source rather than full vectors for every channel.

For backtest this means the event clock advances by decoded event batches. The
decoder feeds only the next needed rows, and the backtest engine does not know
whether the source was JSON, `.hfc`, or a future custom binary pipeline.

## CXETCPP relationship

`CXETCPP` remains the algo-facing API boundary.

Live capture path:

```text
exchange/network data
  -> CXETCPP public stream APIs
  -> hft-recorder CxetCaptureBridge
  -> recorder-owned normalized rows
  -> canonical session corpus
  -> optional hft-compressor .hfc artifacts
```

Future compressed replay/backtest path:

```text
.hfc artifacts
  -> hft-compressor streaming decoder
  -> recorder-owned normalized replay rows
  -> ReplayTimelineMerger / ReplayClock
  -> CXETCPP local market-data source for hftrecorder_local
  -> CXETCPP subscribe/fanout APIs
  -> algorithm code and other subscribers
```

The algorithm must talk through CXETCPP, not through recorder DTOs or storage
paths. Recorder is the bridge that converts compressed corpus data into the
same semantic market-data stream that an algo would receive from the CXETCPP
side.

`hftrecorder_local` is a local exchange id, not a hidden `is_backtest` switch.
Algorithms should subscribe to it through ordinary CXETCPP builders in the same
style as real exchange subscriptions.

Rules:

- `hft-compressor` must not depend on `CXETCPP` internals.
- `CXETCPP` must not own `.hfc` storage policy.
- `hft-recorder` owns corpus semantics, conversion, validation, viewer, and
  future compressed replay orchestration.
- Decoded data is streamed into consumers; it is not expanded as a second full
  corpus in memory.

## Implementation status

Implemented now:

- `Compress` tab can call the prebuilt `hft-compressor` library.
- Pipeline registry is visible to the tab.
- `std.zstd_jsonl_blocks_v1` can produce `.hfc` and metrics.
- `cxet_replay_core` exposes callback-based session streaming.
- `cxet_replay_core` can prefer `.hfc` channel artifacts when it is linked
  with the prebuilt `hft-compressor` library.
- `hft-recorder` recorded-session loading uses the CXET replay stream API in
  CXET-enabled builds, then materializes rows for the current GUI viewer.

Not implemented yet:

- merged replay timeline fanout from recorded rows into CXETCPP subscribers;
- GUI compressed-source selection/discovery;
- compressed session manifest/index discovery;
- streaming `.hfc` file decoder API that does not require loading the whole
  compressed file buffer;
- CXETCPP-facing replay/backtest adapter over compressed sources.
