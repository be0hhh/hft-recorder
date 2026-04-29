# Local Exchange Config

Этот документ описывает конфигурацию `hftrecorder_local`.

## Current Ports

```text
HFTREC_OBJECT_WS_HOST=127.0.0.1
HFTREC_OBJECT_WS_PORT=18081
HFTREC_OBJECT_WS_MODE=off optional disable switch
CXET_HFTREC_WS_HOST=127.0.0.1
CXET_HFTREC_WS_PORT=18081
CXET_HFTREC_WS_PATH=/api/v1/local-venue/ws
```

Target market-data WS:

```text
HFTREC_MARKETDATA_WS_HOST=127.0.0.1
HFTREC_MARKETDATA_WS_PORT=18082
HFTREC_MARKETDATA_WS_MODE=off optional disable switch
CXET_HFTREC_MARKETDATA_WS_HOST=127.0.0.1
CXET_HFTREC_MARKETDATA_WS_PORT=18082
CXET_HFTREC_MARKETDATA_WS_PATH=/api/v1/local-marketdata/ws
```

## Local API Keys

CXETCPP credential resolver supports generic slots. For local exchange use:

```text
HFTRECORDER_LOCAL_API_1_KEY=local-key
HFTRECORDER_LOCAL_API_1_SECRET=local-secret
HFTRECORDER_LOCAL_API_1_PASSPHRASE=optional
```

Market-specific overrides may be used:

```text
HFTRECORDER_LOCAL_FUTURES_API_1_KEY=local-key
HFTRECORDER_LOCAL_FUTURES_API_1_SECRET=local-secret
```

Private WS auth accepts timestamps within:

```text
HFTREC_LOCAL_AUTH_WINDOW_NS=5000000000
```

Key purpose:

- authenticate local order/private streams;
- select local account/session state;
- let algo use real private-stream style flows.

Keys do not select strategy behavior and do not make `hft-recorder` a real remote exchange.

## Starting Balance

Target config:

```text
HFTREC_LOCAL_INITIAL_BALANCE_RAW=1000000000000
HFTREC_LOCAL_BALANCE_ASSET=USDT
```

All values are scaled integers. Scale must be documented by instrument/account metadata.

## Fees

Target config fallback:

```text
HFTREC_LOCAL_MAKER_FEE_E8=20000
HFTREC_LOCAL_TAKER_FEE_E8=40000
```

Recorded fee data has priority over config fallback when present.

## Funding

Target config fallback:

```text
HFTREC_LOCAL_FUNDING_MODE=recorded|fixed|off
HFTREC_LOCAL_FUNDING_RATE_E8=0
HFTREC_LOCAL_FUNDING_INTERVAL_NS=28800000000000
```

Default should prefer recorded funding when available. `off` must be explicit.

## Reduce-Only Oversize Policy

Default:

```text
HFTREC_LOCAL_REDUCE_ONLY_OVERSIZE=reject
```

Future optional value:

```text
clamp
```

Reject is safer because hidden clamping changes the strategy-visible order result.


## Replay Runner V1 Defaults

`LocalReplayRunner` is a backend API, not an auto-starting GUI feature yet. GUI can later map its Replay / Live / Archive selector onto this config.

```text
mode=replay
sessionPath=<recorded session directory>
symbolOverride=<optional, used for depth/snapshot rows without per-row symbol>
speedMultiplier=1
repeatCount=1        # 0 means repeat until stopped
maxSpeed=false       # true disables wall-clock waits
startPaused=false
publishSnapshot=true
resetOrderEngineOnStart=true
```

`LiveReserved` and `ArchiveReserved` are intentionally present in the enum but return `Unimplemented` in V1. Backtest-like local trading must use `Replay` mode.

## Future Latency/Ping

Not required in V1 implementation, but config names are reserved:

```text
HFTREC_LOCAL_LATENCY_MODE=off|fixed|uniform|profile
HFTREC_LOCAL_ORDER_IN_DELAY_NS=0
HFTREC_LOCAL_ORDER_OUT_DELAY_NS=0
HFTREC_LOCAL_MARKET_DATA_DELAY_NS=0
HFTREC_LOCAL_PING_MODE=off|fixed|profile
```

Measured local timings and artificial simulated timings must be stored separately.
