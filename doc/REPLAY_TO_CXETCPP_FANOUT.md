# hft-recorder - replay to CXETCPP fanout

Canonical local exchange docs now live in:

- `LOCAL_EXCHANGE_OVERVIEW.md`
- `LOCAL_EXCHANGE_EXECUTION_MODEL.md`
- `LOCAL_EXCHANGE_PROTOCOLS.md`
- `LOCAL_EXCHANGE_REPLAY_AND_FANOUT.md`
- `LOCAL_EXCHANGE_CONFIG.md`
- `LOCAL_EXCHANGE_TESTING.md`

This file remains the focused fanout contract. If there is a conflict, the
`LOCAL_EXCHANGE_*` docs are the newer source of truth for the full local venue
simulation direction.

## Purpose

This document freezes the intended contract for recorded-session replay as a
local exchange stream visible through `CXETCPP`.

The core product rule is:

```text
algo subscribes to CXETCPP
CXETCPP owns algo-facing market-data fanout
hft-recorder owns recorded corpus, replay control, local venue state, GUI, and execution history
```

`hftrecorder_local` is therefore not a special backtest flag. It is a local
exchange id. For an algorithm and for chart consumers, Binance and
`hftrecorder_local` are both data sources that deliver normalized CXETCPP data.

## Current truth

Already available:

- `hft-recorder` records canonical session files: `trades.jsonl`,
  `bookticker.jsonl`, `depth.jsonl`, and `snapshot_*.json`.
- `cxet_replay_core::streamSession()` can stream rows from canonical JSONL and,
  when linked with `hft-compressor`, from `.hfc` artifacts.
- CXETCPP already has `hftrecorder_local` as an exchange id for local order
  intents through `sendWs().object(order)`.
- `hft-recorder` has `LocalOrderEngine`, and live capture already feeds trades
  and bookticker into it.

Important limitation:

- `cxet_replay_core::streamSession()` currently streams by artifact/channel
  order: snapshots, then trades, then bookticker, then depth. That is useful for
  loading or conversion, but it is not a live-like market clock.

Live-like replay requires a merged timeline ordered by:

```text
tsNs, then ingestSeq, then deterministic channel tie-breaker
```

The tie-breaker is only for deterministic delivery inside equal timestamp
buckets. It must not create a claim that one channel semantically caused another
inside the same timestamp.

## Target data path

Recorded replay path:

```text
hft-recorder session or .hfc artifacts
  -> backend-neutral replay reader
  -> ReplayTimelineMerger
  -> ReplayClock
  -> MarketEventBus
  -> CXETCPP local market-data source for hftrecorder_local
  -> CXETCPP subscribe/fanout APIs
  -> algo callbacks, GUI listeners, and other subscribers
```

Local order path stays separate but synchronized by the same replay clock:

```text
algo decision
  -> CXETCPP sendWs().object(order).exchange(hftrecorder_local)
  -> hft-recorder local venue WebSocket
  -> LocalOrderEngine
  -> durable execution events
  -> CXETCPP OrderAck / future user-data stream
  -> hft-recorder GUI and metrics
```

The same replay market event must be applied to both:

- CXETCPP-facing subscribers;
- `hft-recorder` local venue state, such as `LocalOrderEngine`.

This prevents the algorithm from seeing a different market than the local
execution engine uses for simulated fills.

## Public API contract

Algorithms use ordinary CXETCPP subscriptions:

```cpp
cxet::subscribe()
    .object(cxet::composite::out::SubscribeObject::Trades)
    .exchange(cxet::canon::kExchangeIdHftRecorderLocal)
    .market(cxet::canon::kMarketTypeFutures)
    .symbol(symbol);
```

Equivalent local exchange subscriptions should exist for at least:

- trades;
- bookticker;
- orderbook/depth after replay anchoring is stable;
- user/account/order/fill streams after local venue state is durable.

Algorithms use ordinary CXETCPP order calls:

```cpp
cxet::sendWs()
    .object(cxet::composite::out::WsSendObject::Order)
    .exchange(cxet::canon::kExchangeIdHftRecorderLocal)
    .market(cxet::canon::kMarketTypeFutures)
    .symbol(symbol);
```

No algorithm code should branch on `isBacktest`, call `hft-recorder` REST, read
session files, or include recorder DTOs.

## Local market-data wire contract

The first local market-data wire protocol should be recorder/CXET-owned, not a
fake Binance protocol:

The implemented first step reuses the recorder compact JSON arrays directly over
local WebSocket paths. The protocol is selected by query string:

```text
/api/v1/local-marketdata/ws?channel=trades&symbol=btcusdt
/api/v1/local-marketdata/ws?channel=bookticker&symbol=btcusdt
/api/v1/local-marketdata/ws?channel=orderbook.delta&symbol=btcusdt
/api/v1/local-marketdata/ws?channel=funding&symbol=btcusdt
```

Rationale:

- the source is already normalized recorder session data;
- copying Binance JSON would reintroduce exchange-specific parsing semantics;
- the local protocol can carry explicit replay metadata such as session id,
  replay speed, original event timestamp, ingest sequence, and source quality;
- CXETCPP can parse it through a dedicated `hftrecorder_local` adapter and then
  expose normal unified structs to subscribers.

The local market-data protocol is domain data only. It must not carry GUI draw
commands, chart settings, compression settings, or recorder file paths.

Implemented v1 event families:

- `trades` from canonical `trades.jsonl` rows;
- `bookticker` from canonical `bookticker.jsonl` rows;
- `orderbook.delta` from canonical `depth.jsonl` rows;
- `orderbook.snapshot` from canonical snapshot documents;
- `funding` reserved for future funding rows.

Implemented payloads intentionally carry no envelope or metadata because the
recorder files already use compact arrays and CXET route metadata carries
exchange/market/symbol:

```text
trades:          [priceE8, qtyE8, side, tsNs]
bookticker:      [bidPriceE8, bidQtyE8, askPriceE8, askQtyE8, tsNs]
orderbook:       [[priceE8, qtyE8, side], ..., tsNs]
funding future:  [rateE8, nextFundingTsNs, tsNs]
```

## Replay clock contract

Replay must support these modes:

- paused;
- step one timestamp bucket;
- realtime speed;
- fixed multiplier, for example x10;
- max speed for deterministic batch/backtest runs.

The replay clock owns event release. Consumers do not pull arbitrary future
events from the session files.

Ordering rule:

```text
1. Build timestamp buckets by tsNs.
2. Within a bucket, sort by ingestSeq when present.
3. If ingestSeq is missing or equal, use a stable channel tie-breaker.
4. Deliver every item in the bucket before advancing visible replay time.
```

For exact L2 replay, snapshots and depth deltas are the book state truth. Trades
and bookticker are observational overlays and must not mutate reconstructed L2
state.

## Ownership boundaries

`hft-recorder` owns:

- session/corpus schema;
- compressed artifact discovery policy;
- replay UI and controls;
- local venue state;
- execution/event history;
- GUI and metrics presentation.

`hft-compressor` owns:

- `.hfc` container details;
- compression and decompression pipelines;
- streaming decode APIs;
- compression metrics.

`CXETCPP` owns:

- public subscribe/send API;
- normalized market-data structures;
- local exchange route registration;
- local market-data WebSocket/client adapter;
- subscriber fanout and callback timing.

No layer should depend on GUI objects. No strategy should depend on recorder
storage paths. No compressor code should depend on CXETCPP internals.

## Implementation order

1. Document and test `ReplayTimelineMerger` using existing session fixtures.
2. Build a recorder-side `ReplayMarketEventBus` for trades and bookticker only.
3. Feed the same bus into `LocalOrderEngine` and a CXETCPP-facing local market
   data source.
4. Add `hftrecorder_local` subscribe route support in CXETCPP for trades and
   bookticker.
5. Extend to depth/snapshot replay after gap and anchor validation is stable.
6. Add durable execution streams: `orders.jsonl`, `executions.jsonl`,
   `fills.jsonl`, then positions/account state.
7. Add `.hfc` file streaming so compressed artifacts do not require loading the
   full compressed file or decoded corpus into RAM.

## Acceptance checks

- An algorithm can subscribe to `hftrecorder_local` using the same CXETCPP API it
  uses for real exchanges.
- A replay session emits trades and bookticker in monotonically advancing replay
  buckets.
- `LocalOrderEngine` and algorithm subscribers observe the same event order.
- A market order during replay fills or rejects deterministically from the same
  bookticker state visible to subscribers.
- Removing canonical JSONL while keeping valid `.hfc` artifacts still works once
  compressed replay is enabled, without materializing decoded 10 GB sessions in
  memory.
