# Local Exchange Overview

Этот документ фиксирует целевую базу для `hftrecorder_local`: локальная биржа должна выглядеть для алгоритма как обычная live venue, но исполняться внутри `hft-recorder` поверх live/replay market data.

## Цель

`hftrecorder_local` - это не debug marker API и не простой ack server. Это локальная venue-симуляция для replay/backtest/live-sim:

- алгоритм получает market data, order acks, fills, positions и account updates через CXETCPP;
- алгоритм не знает, live это или replay;
- CXETCPP не содержит математику стратегии и не принимает решения по позиции;
- `hft-recorder` владеет replay clock, local exchange state, fills, positions, balance, fees, funding, GUI, metrics и durable execution history.

## Границы слоев

```text
algo
  strategy math, sizing, signals, risk preferences
  talks only to CXETCPP public API

CXETCPP
  public builders, routing, WS/REST transport, local adapters, normalized fanout
  no exchange simulation math
  no chart objects

hft-recorder
  corpus, replay clock, local venue logic, execution/account state, GUI, metrics
  converts recorded/live market data into local exchange behavior
```

Постоянное правило: algo не читает recorder files, recorder DTOs или GUI API. Для него `hftrecorder_local` - обычный `exchange` в CXETCPP.

## Current Truth

Уже есть:

- `sendWs().object(order).exchange(hftrecorder_local)` route в CXETCPP;
- loopback order WebSocket `hftrecorder.local.v1` на `127.0.0.1:18081`;
- `LocalOrderEngine`, который принимает/reject orders, использует L1/trade state, публикует простые execution events;
- GUI marker adapter для текущих Ack/Reject/StateChange;
- canonical session corpus: `trades.jsonl`, `bookticker.jsonl`, `depth.jsonl`, snapshots.

Еще не готово:

- live-like replay clock, который одновременно кормит CXETCPP subscribers и local venue state;
- `hftrecorder.marketdata.v1` adapter для local market-data fanout;
- private/user streams как у реальной биржи;
- durable orders/fills/positions/balance ledger;
- local auth/login/signature;
- full fees/funding/account state;
- latency/ping/jitter simulation.

## Target V1

V1 должен быть минимально полноценным симулятором без лишних аккаунтных сложностей:

- одна локальная venue и один replay source за раз;
- ключи событий сразу multi-venue-ready: `session_id`, `source_id`, `exchange_raw`, `market_raw`, `symbol`;
- one-way signed position per symbol;
- balance/equity без leverage;
- open/close position lifecycle;
- `reduceOnly` validation;
- market и limit orders;
- maker/taker fee;
- funding from recorded data, config fallback;
- private streams: orders, fills, positions, balance/account;
- local API key auth для private/order paths;
- no slippage и no book depletion в V1.

## Public API Shape

Алгоритм использует те же CXETCPP builders, что и для реальной биржи:

```cpp
cxet::subscribe()
    .object(cxet::composite::out::SubscribeObject::Trades)
    .exchange(cxet::canon::kExchangeIdHftRecorderLocal)
    .market(cxet::canon::kMarketTypeFutures)
    .symbol(symbol);

cxet::sendWs()
    .object(cxet::composite::out::WsSendObject::Order)
    .exchange(cxet::canon::kExchangeIdHftRecorderLocal)
    .market(cxet::canon::kMarketTypeFutures)
    .symbol(symbol);
```

Private/user subscriptions тоже идут через CXETCPP, а не напрямую в `hft-recorder`.

## Multi-Venue Direction

V1 реализует одну venue, но модель не должна привязываться к singleton-symbol truth:

- market events keyed by `session_id + source_id + exchange_raw + market_raw + symbol`;
- execution events keyed by `venue_id/exchange_raw + account_id + symbol + order_id/client_order_id`;
- replay clock допускает future multi-source merge;
- GUI layers не должны предполагать, что на chart есть только одна биржа.

Полноценная two-venue arbitrage replay - future phase, не V1.

## Non-Goals For V1

- leverage and liquidation engine;
- real queue-position claims;
- slippage and orderbook depletion;
- exchange-specific margin rules;
- exact SBE/tick-by-tick microstructure claims from degraded JSON BBO;
- moving strategy sizing into CXETCPP or hft-recorder.

## Source Of Truth

Читайте вместе:

- `LOCAL_EXCHANGE_EXECUTION_MODEL.md` - execution/account semantics;
- `LOCAL_EXCHANGE_PROTOCOLS.md` - local WS protocols and auth;
- `LOCAL_EXCHANGE_REPLAY_AND_FANOUT.md` - replay clock and CXETCPP fanout;
- `LOCAL_EXCHANGE_CONFIG.md` - env/config surface;
- `LOCAL_EXCHANGE_TESTING.md` - acceptance tests.
