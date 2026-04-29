# Local Exchange Execution Model

Этот документ задает поведение local execution engine для `hftrecorder_local`.

## Core Rule

Алгоритм отправляет готовый order intent. Он сам считает размер позиции, percent-of-balance, сигналы и risk preferences. CXETCPP только доставляет request/stream. `hft-recorder` только валидирует и исполняет order как local venue.

## State Model

V1 state:

- `AccountState`: local account id, wallet balance, available balance, equity, realized PnL, unrealized PnL, fee total, funding total;
- `PositionState`: one-way signed quantity per `exchange_raw + market_raw + symbol`, avg entry price, realized PnL, unrealized PnL, last mark price;
- `OrderState`: client order id, local order id, side, type, time-in-force, reduce-only, qty, filled qty, remaining qty, status;
- `FillState`: execution id, order id, side, price, qty, maker/taker flag, fee, realized PnL contribution;
- `FundingState`: funding timestamp, symbol, position qty, mark price, rate, funding amount.

Все деньги, цены, количества и rates хранятся scaled integer primitives. `double` и `float` запрещены.

## Balance Rules

V1 без leverage:

- opening exposure requires enough available quote balance under 1x notional rules;
- closing exposure releases notional and realizes PnL;
- fees reduce wallet/available balance immediately on fill;
- funding updates wallet/available balance at funding timestamp;
- unrealized PnL is recomputed from mark/bookticker when available.

Exact margin/leverage/liquidation rules are future work.

## Position Rules

One-way signed position:

- buy increases positive exposure or reduces short exposure;
- sell increases negative exposure or reduces long exposure;
- if a trade crosses through zero, split internally into close part and open part;
- avg entry updates only for the opening/increasing side;
- realized PnL updates only on closing quantity.

## Reduce-Only

`reduceOnly=true` means the order may only reduce existing exposure:

- reject if current position is zero;
- reject buy reduce-only when position is long or zero;
- reject sell reduce-only when position is short or zero;
- if order qty is larger than closeable position, V1 should clamp or reject by explicit config; default is reject to avoid hidden behavior.

## Order Types

V1 supports:

- Market;
- Limit;
- Stop/StopLoss as trigger-to-market behavior if already present in engine.

Future:

- cancel;
- replace/amend;
- post-only;
- IOC/FOK/GTX/GTD fidelity;
- exchange-specific order flags.

## Fill Rules

Market order:

- buy fills at current ask;
- sell fills at current bid;
- reject if no valid L1/bookticker exists for symbol;
- no slippage and no book depletion in V1.

Limit order:

- rests until marketable by observed trade/bookticker state;
- buy limit can fill at limit or better when market reaches it;
- sell limit can fill at limit or better when market reaches it;
- current simple exact-trade behavior may remain during transition, but target docs require marketable semantics;
- no queue-position claim in V1.

Partial fills:

- event types and ledger fields must support partial fills;
- V1 may fill full remaining qty unless L2/depth queue mode is enabled later.

## Fees

Fee source priority:

1. recorded exchange fee/fill data when present;
2. config maker/taker fee rates;
3. zero fee only with explicit warning or config.

Fee event must store:

- fee amount;
- fee currency/asset;
- maker/taker classification;
- order id and exec id;
- timestamp.

## Funding

Funding source priority:

1. recorded funding rows for the historical session;
2. config funding schedule/rate fallback;
3. no funding only with explicit warning or config.

Funding applies to open position at funding timestamp and emits account/private update.

## Execution Events

Internal execution events must be richer than the current socket `OrderAck`:

- `OrderAccepted`;
- `OrderRejected`;
- `OrderTriggered`;
- `OrderPartiallyFilled`;
- `OrderFilled`;
- `OrderCanceled`;
- `PositionChanged`;
- `BalanceChanged`;
- `FundingApplied`;
- `FeeCharged`.

The current `Ack`, `Reject`, `StateChange` can remain wire-compatible while the internal domain becomes additive.

## Durable Streams

Target recorder files:

```text
orders.jsonl
fills.jsonl
executions.jsonl
positions.jsonl
balances.jsonl
funding_ledger.jsonl
```

GUI and metrics consume these events; CXETCPP does not receive chart-specific fields.
