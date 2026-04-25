# hft-recorder Local Exchange Future Plan

This document answers one practical question: what part of the local-exchange architecture is already real, and what still needs to be built.

Current date: 2026-04-25

## Short Answer

The foundation is already in place, but the full system from your description is not complete yet.

Already real:

- hft-recorder can receive local WebSocket order intents from CXETCPP on `127.0.0.1:18081`.
- CXETCPP can route `sendWs().object(order)` to the hft-recorder local venue when the exchange id is `hftrecorder_local`.
- hft-recorder has `LocalOrderEngine`, which validates orders, keeps simple market state, accepts/rejects orders, and emits execution events.
- live capture currently feeds trades and bookticker into `LocalOrderEngine` while also writing canonical JSONL.
- hft-recorder returns an ack/reject response to CXETCPP.
- hft-recorder GUI can visualize current `Ack`/`Reject` execution events as vertical chart markers.
- REST marker API is separate and remains only a debug/offline/manual marker tool.

Not fully real yet:

- recorded files are not replayed as a true live-like stream into both algorithm and local venue in one synchronized timeline;
- hft-recorder is not yet a full mock exchange with subscriptions, fills stream, account state, positions, cancel/replace, and durable execution history;
- latency tracing is not complete end-to-end from algo signal to CXETCPP send, WebSocket receive, local venue processing, ack send, and ack receive;
- artificial ping/jitter/delay is not implemented as a configurable exchange/network simulation layer;
- GUI currently draws only simple vertical markers for ack/reject, not complete order/fill/position overlays;
- Grafana does not yet have the full local-exchange latency dashboard.

So the right statement is: the base contract and first working vertical slice exist, but the complete local exchange/backtest/live-simulation loop is still a future plan.

## Intended Architecture

There are three independent corners:

```text
1. CXETCPP
   exchange connector, parser, public API, normalized request/market-data path

2. hft-recorder
   GUI, canonical corpus, replay, local venue, visualization, metrics, future compression lab

3. algo
   mathematical decision engine; reads market data and sends orders through CXETCPP only
```

The core rule:

```text
algo never talks directly to hft-recorder for trading
algo talks to CXETCPP
CXETCPP talks to real exchange or hft-recorder local venue
hft-recorder decides how to visualize/store execution events
```

## Live Market Mode: Target

Target flow for real exchange in the moment:

```text
real exchange network
  -> CXETCPP websocket/parser
  -> normalized market data
  -> hft-recorder capture/corpus/local venue state
  -> algo market-data processor

algo decides to trade
  -> CXETCPP sendWs order builder
  -> CXETCPP local venue client
  -> hft-recorder LocalVenueWsServer
  -> hft-recorder LocalOrderEngine
  -> ack/reject/fill events
  -> CXETCPP OrderAck back to algo
  -> hft-recorder GUI/statistics/metrics visualization
```

What is already close:

- hft-recorder capture uses CXETCPP subscriptions for trades and bookticker;
- captured trade/bookticker rows are fed into `LocalOrderEngine`;
- orders can come from CXETCPP into hft-recorder over local WebSocket;
- local engine can use current bookticker/trade state for simple execution logic.

What is still missing:

- a clean shared market-data delivery contract to feed the algorithm at the same time and with the same event order;
- explicit timestamp trace attached to every event across the whole route;
- real exchange-like fill stream and account/position stream back to the algorithm.

## Recorded Data Mode: Target

Target flow for recorded data:

```text
recorded canonical JSONL/corpus
  -> replay clock
  -> CXETCPP-compatible market-data stream
  -> hft-recorder local venue state
  -> algo market-data processor

algo sends order through CXETCPP
  -> same local venue WebSocket path as live mode
  -> hft-recorder local engine processes order at simulated effective exchange time
  -> ack/fill events return through CXETCPP
  -> GUI draws what happened on the historical chart
```

Required property:

```text
the algo should not know whether data is live or replayed
```

What is already available:

- canonical JSONL files exist for trades/bookticker/depth/snapshots;
- GUI can load historical `trades.jsonl` and session folders;
- replay parsing and chart rendering exist;
- local order engine has methods for trade/bookticker updates.

What is not complete:

- there is no finished replay streamer that feeds historical rows into CXETCPP/algo and `LocalOrderEngine` in one controlled timeline;
- replay currently loads and visualizes data, but it is not yet a real exchange-like event clock for algorithms;
- no deterministic backtest order/execution timeline is persisted yet.

## Current Implementation Map

Current hft-recorder order path:

```text
src/gui/api/LocalVenueWsServer.*
  receives WebSocket JSON protocol hftrecorder.local.v1
  accepts op=order.submit
  builds CXETCPP OrderRequestFrame
  calls globalLocalOrderEngine().submitOrder(...)
  returns ack JSON

src/core/local_exchange/LocalOrderEngine.*
  validates order shape
  stores simple per-symbol market state
  fills market orders from current bookticker
  keeps pending limit/stop orders
  processes pending orders on trade/bookticker updates
  emits ExecutionEvent

src/gui/api/ExecutionChartAdapter.*
  receives ExecutionEvent
  currently draws Ack/Reject as vertical chart markers
```

Current live market-data feed into local engine:

```text
src/core/capture/CaptureCoordinatorRuntime.cpp
  captured trades -> globalLocalOrderEngine().onTrade(...)
  captured bookticker -> globalLocalOrderEngine().onBookTicker(...)
```

Current GUI wiring:

```text
src/gui/app/main.cpp
  starts LocalExchangeServer
  starts LocalVenueWsServer
  wires LocalOrderEngine events to ExecutionChartAdapter
```

## What hft-recorder Can Do Now

### 1. Receive order intent from CXETCPP

Status: works as first version.

CXETCPP sends:

```json
{
  "protocol": "hftrecorder.local.v1",
  "op": "order.submit",
  "request_id": 1,
  "client_send_ts_ns": "1776994215000000000",
  "order": {
    "exchange_raw": 250,
    "market_raw": 1,
    "type_raw": 0,
    "side_set": 1,
    "side_raw": 1,
    "price_raw": 0,
    "quantity_raw": 30,
    "symbol": "RAVEUSDT"
  }
}
```

hft-recorder returns ack/reject.

### 2. Use current local market state

Status: partially works.

For market orders, current engine needs bookticker state:

- buy fills at current ask;
- sell fills at current bid;
- if no bookticker exists, market order rejects with `MissingBookTicker`.

Limit/stop orders can stay pending and react to later trade/bookticker events.

### 3. Visualize execution events

Status: minimal first version.

Current GUI adapter draws vertical markers for:

- `Ack`;
- `Reject`.

It does not yet draw:

- fills;
- arrows;
- order lifecycle states;
- position changes;
- PnL;
- latency annotations.

### 4. Collect some latency/capture metrics

Status: partial.

Some capture-side timing probes exist around:

- CXET bridge materialization;
- local engine update;
- JSON rendering;
- event sink write;
- recorder metrics update.

But the full order path trace does not exist yet.

## What hft-recorder Cannot Do Yet

### 1. It is not yet a complete mock exchange

Missing exchange features:

- order cancel;
- order replace/amend;
- fill stream;
- account stream;
- position stream;
- balances/margin;
- partial fills;
- fees/slippage rules;
- durable execution log;
- replayable execution corpus.

### 2. It is not yet a live-like replay source

Recorded JSONL can be loaded and visualized, but replay is not yet a synchronized market-data server for algorithms.

Need:

```text
ReplayClock -> MarketEventBus -> CXETCPP/algo + hft-recorder local engine + GUI
```

### 3. It does not yet simulate network latency

Current local WebSocket measures real local technical overhead only.

Need configurable delay model:

```text
base_delay_ns
jitter_min_ns
jitter_max_ns
packet_loss_rate maybe later
market_data_delay_ns
order_send_delay_ns
ack_delay_ns
```

### 4. It does not yet measure full order lifecycle latency

Need a trace object carried across the path:

```text
signal_detected_ts_ns
algo_order_build_start_ts_ns
algo_order_build_done_ts_ns
cxet_send_enter_ts_ns
cxet_ws_write_done_ts_ns
recorder_ws_recv_ts_ns
local_engine_start_ts_ns
local_engine_done_ts_ns
effective_exchange_ts_ns
recorder_ack_send_ts_ns
cxet_ack_recv_ts_ns
algo_ack_visible_ts_ns
```

For CPU-local precision, use TSC/rdtsc where available and store wall-clock ns for correlation with chart data.

## Future Plan

## Phase 1: Freeze Contracts

Goal: make the current split explicit and stable.

Tasks:

1. Keep REST marker API only for manual/debug/offline chart markers.
2. Keep WebSocket local venue only for domain trading events.
3. Keep CXETCPP unaware of GUI objects.
4. Version all local venue messages under `hftrecorder.local.v1`.
5. Document exchange id `hftrecorder_local` as the only local mock exchange target.

Done when:

- REST guide and WebSocket guide stay separate;
- algo order path does not mention drawing;
- GUI visualization subscribes only to execution events.

## Phase 2: Execution Event Store

Goal: every local venue action must become a durable event.

Add events:

- order accepted;
- order rejected;
- order triggered;
- order filled;
- order partially filled;
- order canceled;
- position changed;
- account changed.

Add storage:

```text
executions.jsonl
orders.jsonl
fills.jsonl
positions.jsonl later
```

Done when:

- GUI can be closed/reopened and still show historical local execution events;
- backtest can compare expected and actual execution timeline;
- Grafana can count orders/fills/rejects from stored event stream.

## Phase 3: Replay Market Event Bus

Goal: recorded data must behave like live data.

Add module:

```text
src/core/replay_runtime/
  ReplayClock
  MarketEvent
  MarketEventBus
  ReplayStreamer
```

Responsibilities:

- read canonical JSONL/corpus;
- merge trades/bookticker/depth by timestamp and sequence;
- emit events in deterministic order;
- support speed modes: paused, step, realtime, x10, max;
- feed the same event to local venue state and algo-facing CXETCPP path.

Done when:

```text
recorded corpus -> replay streamer -> local engine + algo
```

works without changing algo logic.

## Phase 4: CXETCPP-compatible Replay Connector

Goal: the algorithm should see replay data through the same conceptual CXETCPP market-data interface.

Possible approaches:

1. Add CXETCPP replay transport that reads hft-recorder replay events.
2. Add hft-recorder local market-data WebSocket that mimics exchange streams and let CXETCPP parse it.
3. Add in-process adapter for benchmark/backtest mode.

Recommended order:

1. start with in-process/local adapter for deterministic testing;
2. add WebSocket market-data server after the event model is stable;
3. only then mimic real exchange protocols if needed.

Done when:

- the algo code can switch between real exchange and recorded replay by config/exchange id;
- no algo code branches on `is_backtest`.

## Phase 5: Latency Trace

Goal: measure every important stage.

Add a `LatencyTrace` or `OrderTrace` structure with:

```text
request_id
client_order_id
symbol
side
type
signal_ts_ns
algo_build_start_tsc
algo_build_done_tsc
cxet_send_enter_tsc
cxet_ws_write_done_tsc
recorder_ws_recv_tsc
recorder_engine_enter_tsc
recorder_engine_done_tsc
recorder_ack_write_tsc
cxet_ack_recv_tsc
algo_ack_done_tsc
wall_clock_anchor_ns
```

Rules:

- TSC is for local stage duration;
- wall-clock ns is for chart/corpus alignment;
- do not mix them without an anchor/calibration step.

Done when:

- Grafana can show algo build time, CXET send time, local WebSocket time, hft-recorder processing time, total ack time;
- execution events can show latency labels on chart.

## Phase 6: Artificial Delay Model

Goal: simulate real exchange/network timing without hiding local overhead.

Add config:

```text
HFTREC_LOCAL_VENUE_DELAY_MODE=off|fixed|uniform|normal|profile
HFTREC_ORDER_IN_DELAY_NS=0
HFTREC_ORDER_OUT_DELAY_NS=0
HFTREC_ORDER_JITTER_MIN_NS=0
HFTREC_ORDER_JITTER_MAX_NS=0
HFTREC_MARKET_DATA_DELAY_NS=0
```

Important rule:

```text
effective_exchange_ts = signal/arrival time + measured local cost + configured artificial delay
```

Do not overwrite real measured local timings. Store both:

- real local processing timestamps;
- simulated effective exchange timestamps.

Done when:

- same replay can run with 0 ms, 1 ms, 10 ms, or random latency profiles;
- chart can show where the algo wanted to trade and where the simulated exchange actually accepted/filled it.

## Phase 7: Rich GUI Visualization

Goal: visualization becomes a module over execution events.

Add layers:

- signal line;
- order accepted marker;
- reject marker;
- fill arrow;
- pending order line;
- position region;
- latency label;
- per-order details panel.

Rules:

- CXETCPP still sends no draw commands;
- hft-recorder chooses visualization from event type;
- user can turn layers on/off.

Done when:

- a backtest can be visually inspected without reading logs;
- clicking a marker shows full order trace and latency trace.

## Phase 8: Metrics And Grafana

Goal: Grafana shows whether the system works and where time is spent.

Metrics to add:

```text
hftrec_local_orders_total
hftrec_local_order_rejects_total
hftrec_local_order_fills_total
hftrec_local_order_ack_latency_ns
hftrec_local_engine_process_ns
hftrec_local_ws_recv_to_ack_ns
hftrec_local_artificial_delay_ns
hftrec_replay_events_total
hftrec_replay_lag_ns
hftrec_algo_signal_to_order_ns
hftrec_cxet_order_send_ns
```

Dashboards:

- local venue health;
- order lifecycle latency;
- replay speed/lag;
- rejects by reason;
- fills by symbol/type;
- capture pipeline latency.

Done when:

- standard ports still work: Grafana `3000`, Prometheus `9090`, metrics `8080`;
- order path is visible without opening debugger/logs.

## Phase 9: Deterministic Tests

Goal: make the local exchange testable.

Test scenarios:

1. no bookticker -> market order rejects;
2. bookticker exists -> market buy fills at ask;
3. bookticker exists -> market sell fills at bid;
4. limit buy fills only on matching sell trade;
5. stop order triggers and fills after bookticker;
6. replay corpus produces same fills on every run;
7. artificial delay shifts effective exchange timestamp but does not erase measured local timestamps;
8. GUI adapter receives event but CXETCPP never receives chart-specific fields.

Done when:

- CI/local test run can prove the mock exchange behavior is stable;
- changing visualization cannot break order execution semantics.

## Practical Next Step

The next best implementation task is not to add more drawing. The next best task is:

```text
Build ReplayMarketEventBus and feed recorded trades/bookticker into LocalOrderEngine with a deterministic replay clock.
```

After that, connect the same replay bus to the algo/CXETCPP-facing market-data path.

Reason: without replay-as-live event flow, the local exchange cannot be trusted for backtests. Visualization and Grafana become much more useful after execution events are driven by the same deterministic timeline as the algorithm.

## Final Boundary

Keep this boundary permanently:

```text
CXETCPP = domain transport and normalized trading API
hft-recorder = local venue, corpus, replay, visualization, metrics
algo = decision making only
```

Never move chart drawing into CXETCPP.
Never make algo call hft-recorder REST for trading.
Never make replay mode require different algo logic.
