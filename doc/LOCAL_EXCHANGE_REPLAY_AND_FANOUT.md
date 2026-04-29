# Local Exchange Replay And Fanout

Этот документ фиксирует, как recorded/live market data становится live-like exchange stream для CXETCPP, local venue и GUI.

## Core Rule

Один market event order должен быть виден всем consumers:

```text
ReplayClock / LiveIngress
  -> MarketEventBus
  -> CXETCPP local market-data adapter
  -> algo subscribers
  -> LocalOrderEngine state
  -> GUI/metrics observers
```

Алгоритм и local execution engine не должны видеть разные рынки.

## Replay Timeline

Recorded rows merge order:

```text
1. tsNs
2. ingestSeq when present
3. deterministic channel tie-breaker
```

Tie-breaker нужен только для deterministic delivery внутри одного timestamp bucket. Он не создает утверждение, что одно событие реально произошло раньше другого внутри ambiguous bucket.

## Clock Modes

Implemented V1 backend controls:

- paused/resumed by runner state;
- realtime via `speedMultiplier=1`;
- fixed integer multiplier such as `speedMultiplier=1000`;
- max deterministic batch mode via `maxSpeed=true`;
- finite repeat or loop-until-stopped via `repeatCount`.

Step-one-bucket is reserved for the future GUI controller. Consumers do not pull future rows directly from session files; the clock owns release.


Implemented backend hook:

- `LocalReplayRunner` loads `SessionReplay`, releases existing timestamp buckets, publishes compact recorder arrays into `LocalMarketDataBus`, and updates `LocalOrderEngine` from the same trades/bookticker before public fanout reaches clients.
- V1 supports replay mode only. Live and archive are reserved modes so GUI can expose the future selector without changing the backend shape.
- `repeatCount=0` means loop until stopped; `maxSpeed=true` keeps deterministic order but removes wall-clock waits.

## Event Families

Phase 1:

- trades;
- bookticker;
- funding/mark price if available for account/funding simulation.

Phase 2:

- depth delta;
- snapshot anchor;
- reconstructed L2 state after sequence/gap validation.

## CXETCPP Fanout

CXETCPP owns algo-facing fanout. `hft-recorder` owns source/corpus/replay semantics.

Target path:

```text
session JSONL or .hfc artifacts
  -> backend-neutral reader
  -> ReplayTimelineMerger
  -> ReplayClock
  -> hftrecorder.marketdata.v1 WS
  -> CXETCPP hftrecorder_local adapter
  -> normal subscribe callbacks
```

The local adapter must expose ordinary CXETCPP normalized events. Algo code must not include recorder headers.

## Local Venue State

The same released event updates local venue state:

- bookticker updates current L1;
- trade updates last trade and can trigger resting orders;
- funding applies to open position at funding timestamp;
- mark price updates unrealized PnL.

## GUI

GUI consumes recorder-owned event streams and execution ledger. GUI must not be the primary source of execution truth.

Correct:

- GUI draws order/fill/position overlays from execution events;
- GUI can show replay position, balance, fee, funding timeline;
- GUI can filter by venue/source/symbol.

Wrong:

- CXETCPP sends `draw_line` or chart-specific commands;
- algo calls REST marker API for trading;
- replay mode changes algo logic.

## Compressed Replay

`.hfc` or future compressed artifacts are storage backends only. Decoded rows must stream into the same timeline/event bus and must not be materialized as a second full 10 GB corpus in memory.

## Multi-Venue Future

V1 runs one venue, but replay event keys must support future:

- two independent session sources;
- shared global clock;
- per-venue latency profiles;
- arbitrage algorithms subscribing to both venues through CXETCPP.

Do not hardcode singleton market state into public contracts.
