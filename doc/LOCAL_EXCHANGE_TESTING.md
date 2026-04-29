# Local Exchange Testing

Этот документ задает acceptance checks для `hftrecorder_local`.

## Test Policy

Execution behavior must be tested independently from GUI drawing. GUI tests may verify overlays, but order/fill/account truth lives in local exchange domain tests.

## Required Unit Tests

Execution engine:

- market order without bookticker rejects;
- market buy fills at ask;
- market sell fills at bid;
- limit order rests and fills deterministically when marketable;
- stop/stop-loss trigger behavior remains deterministic;
- reduce-only reject when order increases exposure;
- reduce-only reject when current position is zero;
- open position updates qty and avg entry;
- close position updates realized PnL and balance;
- fee is charged on fill;
- funding is applied at funding timestamp.

Auth:

- missing login rejects private/order stream;
- unknown key rejects;
- bad signature rejects;
- stale timestamp rejects;
- valid local HMAC signature accepts.

Replay:

- rows merge by `tsNs, ingestSeq, tie-breaker`;
- equal timestamp delivery is deterministic;
- replay emits same fills on repeated runs;
- LocalOrderEngine and algo-facing subscriber observe the same market event order.

CXETCPP adapter:

- `subscribe(...hftrecorder_local...)` receives normalized trades/bookticker;
- private user stream receives order/fill/position/balance updates;
- algo does not include recorder headers;
- network layer does not parse JSON.

## Integration Scenarios

Scenario 1: basic replay market order

```text
session bookticker -> replay clock -> CXETCPP subscriber + LocalOrderEngine
algo sends market buy through CXETCPP
local engine fills at ask
private stream emits order/fill/position/balance
GUI draws fill from execution event
```

Scenario 2: reduce-only close

```text
position long exists
algo sends reduce-only sell
local engine closes or partially closes position
balance and realized PnL update
private stream emits position and balance update
```

Scenario 3: replay determinism

```text
same session + same orders + same config
run twice
orders/fills/positions/balances match byte-for-byte except allowed run ids
```

Scenario 4: local auth

```text
CXETCPP resolves HFTRECORDER_LOCAL_API_1_KEY/SECRET
login frame signed with HMAC-SHA256
hft-recorder accepts private stream
bad signature is rejected
```

## Non-Regression Rules

- Adding GUI overlays must not change fills.
- Adding compression backend must not change replay event order.
- Adding market-data delay must not erase measured local timings.
- Adding multi-venue support must not remove single-venue deterministic behavior.
