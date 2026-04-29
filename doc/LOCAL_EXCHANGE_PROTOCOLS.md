# Local Exchange Protocols

Этот документ задает local WS protocol family между CXETCPP и `hft-recorder`.

## Protocol Families

```text
hftrecorder.local.v1       order/private venue protocol
hftrecorder.marketdata.v1  replay/live-sim market-data protocol
```

Оба протокола domain-only. Они не переносят GUI commands, chart drawing, file paths или compression settings.

## Ports

Defaults:

```text
order/private WS: 127.0.0.1:18081 /api/v1/local-venue/ws
market-data WS:   127.0.0.1:18082 /api/v1/local-marketdata/ws
REST marker API:  127.0.0.1:18080, unrelated debug tool
metrics:          127.0.0.1:8080 /metrics
```

Order/private and market-data may share one process, but their protocol contracts stay separate.

## Auth

Local private/order paths require login/signature in target V1.

Credential env names:

```text
HFTRECORDER_LOCAL_API_1_KEY
HFTRECORDER_LOCAL_API_1_SECRET
HFTRECORDER_LOCAL_API_1_PASSPHRASE optional
HFTRECORDER_LOCAL_FUTURES_API_1_KEY optional market-specific override
HFTRECORDER_LOCAL_FUTURES_API_1_SECRET optional market-specific override
```

Auth method:

- HMAC-SHA256 hex;
- timestamp in ns or ms, fixed by protocol message;
- reject missing key, unknown key, stale timestamp, bad signature;
- public market-data replay may stay unauthenticated on loopback in V1.

Implemented login frame:

```json
{
  "protocol":"hftrecorder.local.v1",
  "op":"auth.login",
  "request_id":1,
  "api_key":"local-key",
  "timestamp_ns":1776994215000000000,
  "nonce":"auth-1",
  "target_op":"order.submit",
  "signature":"hex_hmac_sha256"
}
```

Canonical prehash:

```text
api_key + "|" + timestamp_ns + "|" + nonce + "|" + target_op
```

Success response:

```json
{"protocol":"hftrecorder.local.v1","op":"auth.ok","request_id":1,"ok":true,"server_time_ns":"1776994215000123456"}
```

`order.submit` and `user.subscribe` require a successful `auth.login` on the same WebSocket connection.

## Order Submit

Current frame may remain compatible:

```json
{
  "protocol":"hftrecorder.local.v1",
  "op":"order.submit",
  "request_id":1,
  "client_send_ts_ns":"1776994215000000000",
  "order":{
    "exchange_raw":250,
    "market_raw":1,
    "type_raw":1,
    "subtype_raw":255,
    "api_slot_raw":1,
    "side_set":1,
    "side_raw":1,
    "price_raw":0,
    "quantity_raw":100000000,
    "symbol":"BTCUSDT",
    "client_order_id":"optional",
    "time_in_force_raw":0,
    "reduce_only":0
  }
}
```

New fields are additive. Older clients without `reduce_only` default to false.

## Ack Response

`OrderAck` remains minimal for request-response compatibility:

```json
{
  "protocol":"hftrecorder.local.v1",
  "op":"ack",
  "request_id":1,
  "ok":true,
  "recorder_ack_ts_ns":"1776994215000123456",
  "status_raw":0,
  "error_code":0,
  "ts_ns":"1776994215000123000",
  "symbol":"BTCUSDT",
  "order_id":"hftrec-1",
  "client_order_id":"optional"
}
```

Fills, positions and balances must be delivered through private/user streams, not stuffed into ack.

## Private/User Streams

Target private events map to CXETCPP `UserDataEvent` where possible:

- order lifecycle;
- fills/trades;
- positions;
- balance/account;
- fees;
- funding.

Frame example:

```json
{
  "protocol":"hftrecorder.local.v1",
  "op":"user.event",
  "event_type":"fill",
  "session_id":"...",
  "account_id":"local-key-or-account",
  "exchange_raw":250,
  "market_raw":1,
  "symbol":"BTCUSDT",
  "order_id":"hftrec-1",
  "exec_id":"hftfill-1",
  "side_raw":1,
  "price_raw":10100000000,
  "qty_raw":100000000,
  "fee_raw":40400,
  "maker":0,
  "event_ts_ns":"1776994215000123000"
}
```

## Market Data Protocol

The implemented local market-data WebSocket is recorder/CXET-owned and does not mimic Binance JSON.
Clients connect to `/api/v1/local-marketdata/ws?channel=<channel>&symbol=<symbol>`.
The upstream capture/replay stream is already subscribed by `hft-recorder`; local WS clients do not send real exchange subscription frames.

Minimum event families:

- `trades`;
- `bookticker`;
- `orderbook.delta`;
- `orderbook.snapshot`;
- `funding` reserved for future funding frames.

Payloads are exactly the compact recorder arrays, without an envelope:

```text
trades:              [priceE8, qtyE8, side, tsNs]
bookticker:          [bidPriceE8, bidQtyE8, askPriceE8, askQtyE8, tsNs]
orderbook.delta:     [[priceE8, qtyE8, side], ..., tsNs]
orderbook.snapshot:  [[priceE8, qtyE8, side], ..., tsNs]
funding reserved:    [rateE8, nextFundingTsNs, tsNs] or [rateE8, markPriceE8, indexPriceE8, nextFundingTsNs, tsNs]
```

Side convention for local orderbook levels is `0 = bid`, `1 = ask`.

Minimum metadata:

- `protocol`;
- `event_type`;
- `session_id`;
- `source_id`;
- `exchange_raw`;
- `market_raw`;
- `symbol`;
- `ts_ns`;
- `ingest_seq`;
- `source_quality`;
- `source_format`;
- `origin`;
- `feed_kind`.

CXETCPP parses this protocol through the `hftrecorder_local` adapter and fans out normalized structs. Network code only moves bytes; JSON parsing belongs in adapter/parser code.
