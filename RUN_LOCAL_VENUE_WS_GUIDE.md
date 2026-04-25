# hft-recorder Local Venue WebSocket Guide

This file is only about the WebSocket path between CXETCPP and hft-recorder.

This is the future architecture path for local mock exchange behavior. The algorithm talks to CXETCPP. CXETCPP sends a domain order event to hft-recorder. hft-recorder receives it as a local venue, runs internal execution logic, emits execution events, and the GUI may draw lines, arrows, points, fills, statistics, or nothing.

CXETCPP does not draw anything.
CXETCPP does not know about chart objects.
CXETCPP only sends order intent and receives an order ack.

## Mental Model

Target architecture:

```text
algo -> CXETCPP public API -> hftrecorder_local exchange id -> local WebSocket -> hft-recorder local venue
```

Inside hft-recorder:

```text
LocalVenueWsServer -> LocalOrderEngine -> ExecutionEvent -> GUI/statistics/future modules
```

Current v1 visualization:

```text
ExecutionEvent Ack/Reject -> ExecutionChartAdapter -> yellow vertical marker
```

That visualization is only the current GUI adapter. Later the same event can become an arrow, point, order label, fill marker, stats row, or backtest event.

## What This Is Not

This is not REST marker API.
This is not Java/Kafka direct chart drawing.
This is not a chart object protocol.
This is not `algo -> hft-recorder` direct integration.

REST marker guide is in `RUN_MARKER_API_GUIDE.md`.

## Standard Ports

| Service | Port | URL |
| --- | ---: | --- |
| CXET local venue WebSocket | 18081 | ws://127.0.0.1:18081/api/v1/local-venue/ws |
| REST marker API, unrelated | 18080 | http://127.0.0.1:18080/api/v1/health |
| hft-recorder metrics | 8080 | http://127.0.0.1:8080/metrics |
| Grafana | 3000 | http://127.0.0.1:3000 |
| Prometheus | 9090 | http://127.0.0.1:9090 |

## hft-recorder Server Env

The WebSocket server is started by `hft-recorder-gui` only when built with CXETCPP enabled.

Defaults:

```bash
HFTREC_OBJECT_WS_HOST=127.0.0.1
HFTREC_OBJECT_WS_PORT=18081
```

Disable local venue WebSocket:

```bash
HFTREC_OBJECT_WS_MODE=off
```

Use loopback by default. Do not expose this on `0.0.0.0` unless you know why.

## CXETCPP Client Env

Defaults used by CXETCPP local venue client:

```bash
CXET_HFTREC_WS_HOST=127.0.0.1
CXET_HFTREC_WS_PORT=18081
CXET_HFTREC_WS_PATH=/api/v1/local-venue/ws
```

Temporary fallback to old Unix socket transport:

```bash
CXET_HFTREC_TRANSPORT=unix
```

Default is WebSocket. Use Unix fallback only if the new WebSocket server is disabled or while debugging legacy behavior.

## Build Order

Build CXETCPP first:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/src/src'
cmake --build build --target cxet_lib -j2
```

Build hft-recorder with CXETCPP enabled:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/apps/hft-recorder'
./compile.sh
```

The normal `./compile.sh` should use the parent prebuilt `libcxet_lib.so` and public headers.

## Start hft-recorder Local Venue

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/apps/hft-recorder'

HFTREC_METRICS_PORT=8080 \
HFTREC_OBJECT_WS_HOST=127.0.0.1 HFTREC_OBJECT_WS_PORT=18081 \
HFTREC_API_HOST=127.0.0.1 HFTREC_API_PORT=18080 \
./build/start
```

REST env is optional here. It is shown only because the GUI often runs both APIs during development. WebSocket and REST are separate.

## Open Market Data In Viewer

For the current visual marker to appear, Viewer needs an active chart.

Open `Viewer`, then load a recording folder:

```text
C:\Users\be0h\PycharmProjects\course project\CXETCPP\apps\hft-recorder\recordings\1776994215_binance_futures_usd_RAVEUSDT
```

or only trades:

```text
C:\Users\be0h\PycharmProjects\course project\CXETCPP\apps\hft-recorder\recordings\1776994215_binance_futures_usd_RAVEUSDT\trades.jsonl
```

Important: accepting an order and drawing it are separate. hft-recorder may accept/reject an order even if no chart is loaded. Without a loaded chart, the current GUI adapter has nowhere to draw.

## How Algorithm Code Should Use It

Algorithm code should use CXETCPP as if selecting an exchange.

For real Binance:

```cpp
.exchange(cxet::canon::kExchangeIdBinance)
```

For local hft-recorder mock venue:

```cpp
.exchange(cxet::canon::kExchangeIdHftRecorderLocal)
```

Example order intent:

```cpp
#include "cxet.hpp"
#include "api/run/RunByConfig.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Enums.hpp"
#include "composite/level_0/SendWsObject.hpp"
#include "primitives/buf/MessageBuffer.hpp"
#include "primitives/buf/Symbol.hpp"
#include "primitives/composite/OrderAck.hpp"

MessageBuffer payloadBuf{};
MessageBuffer recvBuf{};
cxet::composite::OrderAck ack{};

cxet::Symbol symbol{};
symbol.copyFrom("RAVEUSDT");

auto builder = cxet::sendWs()
    .object(cxet::composite::out::WsSendObject::Order)
    .exchange(cxet::canon::kExchangeIdHftRecorderLocal)
    .market(cxet::canon::kMarketTypeFutures)
    .symbol(symbol)
    .type(cxet::canon::OrderType::Market)
    .quantity(cxet::Amount{30})
    .side(cxet::Side{1});

const bool ok = cxet::api::runSendWsByConfig(builder, payloadBuf, recvBuf, &ack);
```

The exact `Amount`/`Side` construction may differ depending on the current CXETCPP primitive helpers used in your algo. The important part is the exchange id:

```cpp
cxet::canon::kExchangeIdHftRecorderLocal
```

## What CXETCPP Sends Over WebSocket

CXETCPP sends a real WebSocket text frame with JSON envelope.

Current protocol:

```json
{
  "protocol": "hftrecorder.local.v1",
  "op": "order.submit",
  "request_id": 1,
  "client_send_ts_ns": 1776994215000000000,
  "order": {
    "exchange_raw": 250,
    "market_raw": 1,
    "type_raw": 0,
    "subtype_raw": 255,
    "side_set": 1,
    "side_raw": 1,
    "price_raw": 0,
    "quantity_raw": 30,
    "symbol": "RAVEUSDT"
  }
}
```

Notes:

- `protocol` is versioned so future messages can be added.
- `op=order.submit` means this is domain order intent, not chart drawing.
- `exchange_raw=250` is `hftrecorder_local`.
- `side_raw=1` means buy in the current local order engine convention.
- `side_raw=0` means sell.
- `type_raw` follows CXETCPP `canon::OrderType`.
- `quantity_raw` and `price_raw` are CXETCPP primitive raw values.

## What hft-recorder Returns

Ack response example:

```json
{
  "protocol": "hftrecorder.local.v1",
  "op": "ack",
  "request_id": 1,
  "ok": true,
  "recorder_ack_ts_ns": "1776994215000123456",
  "status_raw": 3,
  "error_code": 0,
  "ts_ns": "1776994215000123000",
  "symbol": "RAVEUSDT",
  "order_id": "hftrec-1"
}
```

Reject response example:

```json
{
  "protocol": "hftrecorder.local.v1",
  "op": "ack",
  "request_id": 1,
  "ok": false,
  "error": "order_rejected",
  "recorder_ack_ts_ns": "1776994215000123456",
  "status_raw": 4,
  "error_code": 8,
  "ts_ns": "1776994215000123000",
  "symbol": "RAVEUSDT",
  "order_id": ""
}
```

CXETCPP parses this into `composite::OrderAck` for the caller.

## Internal hft-recorder Flow

Current implementation path:

```text
LocalVenueWsServer
  parses WebSocket frame
  validates protocol/op/order JSON
  fills OrderRequestFrame
  calls globalLocalOrderEngine().submitOrder(...)

LocalOrderEngine
  validates symbol/side/type/qty/price
  uses current L1/trade state when needed
  returns OrderAckFrame
  emits ExecutionEvent Ack/Reject/StateChange

ExecutionChartAdapter
  listens to ExecutionEvent
  for Ack/Reject calls ChartController::addVerticalMarker(...)
```

This means future modules should subscribe to execution events, not to the raw WebSocket server.

Correct future additions:

- order arrows in GUI;
- fill markers;
- statistics panels;
- latency metrics;
- backtest execution timeline;
- account/position state.

Wrong future additions:

- adding chart-specific fields to CXETCPP messages;
- making CXETCPP send `draw_line`;
- making algo call hft-recorder REST directly;
- making GUI parse raw CXETCPP builder objects.

## Manual WebSocket Smoke Test

You can test the server without CXETCPP using Python `websockets`:

```python
import asyncio
import json
import websockets

async def main():
    uri = "ws://127.0.0.1:18081/api/v1/local-venue/ws"
    async with websockets.connect(uri) as ws:
        await ws.ping()
        msg = {
            "protocol": "hftrecorder.local.v1",
            "op": "order.submit",
            "request_id": 1,
            "client_send_ts_ns": "1776994215000000000",
            "order": {
                "exchange_raw": 250,
                "market_raw": 1,
                "type_raw": 0,
                "subtype_raw": 255,
                "side_set": 1,
                "side_raw": 1,
                "price_raw": 0,
                "quantity_raw": 30,
                "symbol": "RAVEUSDT",
            },
        }
        await ws.send(json.dumps(msg, separators=(",", ":")))
        print(await ws.recv())

asyncio.run(main())
```

Install dependency if needed:

```bash
python3 -m pip install websockets
```

If market order is rejected with missing L1/bookticker, that means the local engine has no current book ticker for the symbol. That is expected until live/replay market data feeds the local order engine.

## Why A Market Order May Reject

Current `LocalOrderEngine` validates more than message shape.

Examples:

| Condition | Result |
| --- | --- |
| no symbol | reject |
| no quantity | reject |
| side missing | reject |
| unsupported order type | reject |
| market order has price | reject |
| market order but no L1/bookticker | reject |
| limit/stop without price | reject |

For market order fill simulation, hft-recorder needs current L1 book ticker for the symbol. Capture/replay integration will feed that state.

## Diagnostics

Check WS port:

```powershell
netstat -ano | Select-String -Pattern ':18081'
```

Check all related ports:

```powershell
netstat -ano | Select-String -Pattern ':18081|:18080|:8080|:3000|:9090'
```

Check that hft-recorder was built with CXETCPP:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/apps/hft-recorder'
cmake --build build --target hft-recorder-gui -j1
```

Run hft-recorder tests:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/apps/hft-recorder/build'
ctest --output-on-failure
```

Build CXETCPP library:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/src/src'
cmake --build build --target cxet_lib -j2
```

## Common Problems

| Problem | Meaning | Fix |
| --- | --- | --- |
| port `18081` not open | hft-recorder not running, built offline, or WS disabled | start normal CXET build and do not set `HFTREC_OBJECT_WS_MODE=off` |
| CXETCPP order returns false | cannot connect, bad ack, or order rejected | check hft-recorder is running and local order engine has needed market state |
| market order rejected | no current L1/bookticker for symbol | start capture/replay path that feeds bookticker to local engine |
| marker not visible | chart not loaded or timestamp outside viewport | open Viewer data and pan/zoom |
| REST works but CXET order does not | REST and WS are separate systems | debug port `18081` and CXET env, not REST endpoint |
| old Unix socket needed | temporary fallback | set `CXET_HFTREC_TRANSPORT=unix` |

## What To Build Next

The current foundation is ready for future work, but these are separate tasks:

1. Feed replay market data sequentially into CXETCPP/algo and hft-recorder local engine.
2. Add execution event types for fills, position changes, and account state.
3. Add latency fields for signal time, build time, send time, recorder receive time, and effective exchange time.
4. Add artificial delay config for local exchange simulation.
5. Replace the current simple vertical marker adapter with richer order/fill visualization layers.

The core rule stays the same: CXETCPP sends domain trading events; hft-recorder decides how to visualize and store them.
