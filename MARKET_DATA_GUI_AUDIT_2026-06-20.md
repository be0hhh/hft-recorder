# hft-recorder market data GUI audit - 2026-06-20

## Scope

Selected roles:
- primary: `recorder_corpus_engineer`
- secondary: `exchange_normalizer_engineer`, `architect`

Subagents:
- none

Assumptions:
- Public market data only. No private/order routes were exercised.
- Runtime checks used the existing `apps/hft-recorder/build/bin/hft-recorder capture` CLI over the same recorder capture core and writers. I did not automate real QML clicks.
- The live window was 60 seconds per public stream. Zero rows can mean unsupported route, quiet market, market closed, or parser/subscription issue; the audit separates zero rows from process failure.
- Audit date was Saturday, 2026-06-20. FINAM/MOEX live streams are expected to be affected by the closed market.

Latency/safety risks:
- No trading routes were touched.
- CLI probes are runtime network probes, not builds/tests.

Artifacts:
- Main audit root: `/tmp/cxet-recorder-audit-20260620-204214`
- Status files: `capture-status.tsv`, `candles-status.tsv`, `candles2-status.tsv`, `mexc-futures-status.tsv`, `finam-futures-sru6-status.tsv`
- Aggregates: `live-summary.tsv`, `candles-summary.tsv`

## Static GUI matrix

GUI-visible venues come from `src/gui/viewmodels/CaptureViewModelRequests.cpp`:

| GUI key | Exchange | Market |
|---|---:|---:|
| `binance_futures` | binance | futures |
| `binance_spot` | binance | spot |
| `bybit_futures` | bybit | futures |
| `bybit_spot` | bybit | spot |
| `kucoin_futures` | kucoin | futures |
| `kucoin_spot` | kucoin | spot |
| `gate_futures` | gate | futures |
| `gate_spot` | gate | spot |
| `bitget_futures` | bitget | futures |
| `bitget_spot` | bitget | spot |
| `aster_futures` | aster | futures |
| `aster_spot` | aster | spot |
| `okx_futures` | okx | futures |
| `okx_spot` | okx | spot |
| `finam_futures` | finam | futures |
| `finam_spot` | finam | spot |
| `mexc_spot` | mexc | spot |

Notes:
- `mexc_futures` is not exposed in the GUI venue matrix.
- Detailed candles exclude every `mexc` venue in `supportsDetailedCandlesVenue()`, even though `mexc_spot` `candles2` works in the direct recorder probe.
- Core CXETCPP has MEXC futures registered: REST klines/orderbook, public WS trades/orderbook/bookticker/klines/funding/mark/index/open_interest, and order route config are present in `src/src/exchanges/mexc/futures/config.cpp`.

## Runtime method

Live streams used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  <channel> 60 "$AUDIT_ROOT" <exchange> <symbol> <market>
```

Ordinary candles used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  candles 1 "$AUDIT_ROOT" <exchange> <symbol> <market>
```

Detailed candles used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  --timeframe 1m --limit 300 \
  candles2 1 "$AUDIT_ROOT" <exchange> <symbol> <market>
```

Symbols:
- Crypto futures: `BTCUSDT`, except `kucoin futures = XBTUSDTM`, `gate futures = BTC_USDT`, `okx futures = BTC-USDT-SWAP`, direct `mexc futures = BTC_USDT`.
- Crypto spot: `BTCUSDT`, except `kucoin/okx spot = BTC-USDT`, `gate spot = BTC_USDT`.
- FINAM spot: `SBER@MISX`.
- FINAM futures was first run with `SBER@MISX`; a supplementary check used `SRU6@RTSX`.

## Saved formats

Recorder output layout is session based:

```text
<audit-root>/<epoch>_<exchange>_<market>_<symbol>/
  manifest.json
  jsonl/*.jsonl
  reports/integrity_report.json
```

Canonical paths come from `ChannelKind.hpp`:

| Channel | Path | Manifest row schema observed |
|---|---|---|
| trades | `jsonl/trades.jsonl` | `cxet_trade_strict_v1` |
| bookticker | `jsonl/bookticker.jsonl` | `cxet_bookticker_strict_v1` |
| orderbook | `jsonl/depth_tape.jsonl` + `jsonl/depth_sidecar.jsonl` | `cxet_orderbook_tape_rle_sidecar_v1` |
| liquidations | `jsonl/liquidations.jsonl` | `cxet_liquidation_alias_first_v1` |
| candles | `jsonl/candles.jsonl` | `cxet_candle_lite_tiered_v1` |
| candles2 | default `jsonl/candles2.jsonl`; detailed path `jsonl/candles2_<tf>.jsonl` | `cxet_ohlcv_numeric_v3` |
| mark_price | `jsonl/mark_price.jsonl` | `cxet_mark_price_ref_v1` |
| index_price | `jsonl/index_price.jsonl` | `cxet_index_price_ref_v1` |
| funding | `jsonl/funding.jsonl` | `cxet_funding_ref_dedup_v1` |
| price_limit | `jsonl/price_limit.jsonl` | `cxet_price_limit_ref_v1` |

Observed sample row shapes:
- `trades`: `[priceE8, qtyE8, side, tsNs]`
- `bookticker`: `[bidPriceE8, bidQtyE8, askPriceE8, askQtyE8, tsNs]`
- `candles`: `[tier, tsNs, highE8, lowE8, quoteAmountE8]`
- `candles2_1m`: `[tier, tsNs, openE8, highE8, lowE8, closeE8, volumeE8, quoteAmountE8]`

For detailed candles, `captureDetailedCandlesOnce()` writes `jsonl/candles2_<tf>.jsonl`. For tiers mapped by `1m`, `15m`, or `1d`, it also writes compatibility `jsonl/candles_<tf>.jsonl`.

## Live stream results

Legend:
- Numeric cells are rows written in 60 seconds.
- `timeout` means process exit `124` under a 90 second wrapper.
- `0` means process exited cleanly but wrote no rows.

| Venue | trades | bookticker | orderbook | liquidations | mark | index | funding | price_limit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| binance futures BTCUSDT | 50509 | 5554 | 2169 | 0 | 59 | 59 | 2 | 0 |
| binance spot BTCUSDT | 55219 | 1252 | 2230 | 0 | 0 | 0 | 0 | 0 |
| bybit futures BTCUSDT | 1135 | 473 | 2742 | 0 | 38 | 71 | 1 | 35 |
| bybit spot BTCUSDT | 299 | 213 | 2310 | 0 | 0 | 0 | 0 | 33 |
| kucoin futures XBTUSDTM | 57 | 677 | 37535 | 0 | 26 | 41 | 1 | 0 |
| kucoin spot BTC-USDT | 22 | 121 | 14594 | 0 | 0 | 0 | 0 | 0 |
| gate futures BTC_USDT | timeout | timeout | timeout | timeout | timeout | timeout | timeout | timeout |
| gate spot BTC_USDT | 113 | 17 | 9 | 0 | 0 | 0 | 0 | 0 |
| bitget futures BTCUSDT | 528 | 2639 | 1906 | 0 | 157 | 147 | 1 | 0 |
| bitget spot BTCUSDT | 250 | 780 | 1359 | 0 | 0 | 0 | 0 | 0 |
| aster futures BTCUSDT | 75 | 156 | 1234 | 0 | 58 | 59 | 2 | 0 |
| aster spot BTCUSDT | 0 | 42 | 1203 | 0 | 0 | 0 | 0 | 0 |
| okx futures BTC-USDT-SWAP | 1521 | 727 | 1024 | 0 | 291 | 175 | 3 | 84 |
| okx spot BTC-USDT | 220 | 201 | 846 | 0 | 0 | 0 | 0 | 91 |
| finam futures SBER@MISX | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| finam spot SBER@MISX | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| mexc spot BTCUSDT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| mexc futures BTC_USDT direct core probe | 223 | 23 | 270 | 0 | 39 | 29 | 1 | 0 |

Orderbook tape and sidecar row counts matched in the successful probes.

## Candles results

Legend:
- Numeric cells are rows written.
- `timeout` means process exit `124`.
- `fail` means process exit `1`.

| Venue | candles | candles2 1m |
|---|---:|---:|
| binance futures BTCUSDT | 1536 | 300 |
| binance spot BTCUSDT | 1536 | 300 |
| bybit futures BTCUSDT | 1536 | 300 |
| bybit spot BTCUSDT | 1536 | 300 |
| kucoin futures XBTUSDTM | 1536 | 300 |
| kucoin spot BTC-USDT | 1536 | 300 |
| gate futures BTC_USDT | timeout | timeout |
| gate spot BTC_USDT | 1536 | 300 |
| bitget futures BTCUSDT | 1536 | 300 |
| bitget spot BTCUSDT | 1536 | 300 |
| aster futures BTCUSDT | 1536 | 300 |
| aster spot BTCUSDT | 1302 | 300 |
| okx futures BTC-USDT-SWAP | 1536 | 300 |
| okx spot BTC-USDT | 1536 | 300 |
| finam futures SBER@MISX | fail | fail |
| finam spot SBER@MISX | fail | fail |
| mexc spot BTCUSDT | 585 | 300 |
| mexc futures BTC_USDT direct core probe | fail | fail |

Important error text:
- FINAM spot `candles2`: `request=/v1/instruments/SBER@MISX/bars?... interval.start_time=2026-06-20T12:57:40Z&interval.end_time=2026-06-20T17:57:40Z response_bytes=66`.
- FINAM futures `SRU6@RTSX` supplementary `candles2`: same `/v1/instruments/SRU6@RTSX/bars` shape and `response_bytes=66`.
- MEXC futures `candles2`: `request=/api/v1/contract/kline/BTC_USDT?interval=Min1&end=1781978260 response_bytes=0`.
- MEXC futures ordinary `candles`: `m1=FetchFailed count=0`.

## Findings

1. MEXC futures is in core, but not in the GUI.
   - Core registration exists and direct WS probe produced rows for trades, bookticker, orderbook, mark, index, and funding.
   - GUI `kVenues` exposes only `mexc_spot`.
   - Recommendation: add `mexc_futures` to the GUI only with per-channel readiness state. WS streams are usable; REST klines are not ready.

2. MEXC futures REST candles are broken or incomplete.
   - `candles` and `candles2` both fail.
   - The generated direct REST path is `/api/v1/contract/kline/BTC_USDT?interval=Min1&end=...`, but the fetch returned zero bytes in this run.
   - Recommendation: fix/verify the MEXC futures REST kline request builder and transport response handling before enabling detailed candles in GUI.

3. MEXC spot is exposed in GUI, but live WS produced zero rows in 60 seconds.
   - REST candles and `candles2` worked.
   - Live `trades`, `bookticker`, and `orderbook` all exited cleanly but wrote zero rows.
   - Recommendation: treat `exit 0 + 0 rows` as a warning in the GUI/session card, and investigate MEXC spot protobuf WS parsing/subscription.

4. Gate futures is the biggest runtime blocker.
   - Every live channel and both candle paths timed out with no stderr.
   - Gate spot worked.
   - Recommendation: add a visible connect/subscribe timeout reason in recorder, then investigate Gate futures WS/REST handshake or route preparation.

5. FINAM results are inconclusive for live market data on this date.
   - The run happened on Saturday, 2026-06-20.
   - Live streams produced no rows for `SBER@MISX`.
   - `candles` and `candles2` failed for both `SBER@MISX` and supplementary futures symbol `SRU6@RTSX`; the request window was the same Saturday window.
   - Recommendation: add a user-settable end time for `candles2` or a "last trading session" preset, then retest FINAM during an open market window or against a known historical window.

6. Liquidations were not proven anywhere.
   - All liquidations runs wrote zero rows. This may simply be a quiet 60 second window.
   - Recommendation: do not mark liquidation support green from this audit. It needs either a longer window or a synthetic/known active interval.

7. `price_limit` is venue-specific.
   - Rows were observed on Bybit spot/futures and OKX spot/futures.
   - Most other venues wrote zero rows.
   - Recommendation: hide or label unsupported/quiet reference channels per venue instead of showing a uniform "all channels" success.

8. CLI session naming can collide.
   - Supplementary parallel FINAM futures probes that used the same venue/symbol in the same second collided on one session path and produced `session path already exists` / `failed to write initial manifest.json`.
   - Direct MEXC futures parallel probes wrote different channel files into the same session directory, which is usable for row evidence but not a clean per-process manifest model.
   - Recommendation: make recorder session IDs include ns precision, a monotonic suffix, or a short random id; also add a file lock or explicit "existing session" policy.

9. OpenInterest is static-disabled in the recorder GUI.
   - `startOpenInterest()` only reports `OpenInterest display is disabled; no data file was created`.
   - Core MEXC futures config has open_interest route wiring through ticker reference data.
   - Recommendation: either keep it hidden until implemented, or expose it as "not implemented in recorder" rather than as a startable capture action.

## What works

Runtime-proven in this audit:
- Binance futures and spot: core live streams and both candle modes.
- Bybit futures and spot: core live streams and both candle modes.
- KuCoin futures and spot: core live streams and both candle modes.
- Gate spot: core live streams and both candle modes.
- Bitget futures and spot: core live streams and both candle modes.
- Aster futures: core live/reference streams and both candle modes.
- Aster spot: bookticker/orderbook and both candle modes; no trades observed in this 60 second window.
- OKX futures and spot: core live streams and both candle modes.
- MEXC spot: REST candles and `candles2`.
- MEXC futures direct core probe: live WS public streams for trades/bookticker/orderbook/mark/index/funding.

Not runtime-proven or failing:
- Gate futures: timeout across all tested public paths.
- MEXC futures REST candles/candles2: failing.
- MEXC spot live WS: no rows observed.
- FINAM live/candles on the Saturday test window: inconclusive/failing.
- Liquidations: no rows observed anywhere.

## Suggested QoL backlog

1. Add per-venue/channel readiness badges in Capture UI:
   - `ready`
   - `zero rows`
   - `unsupported`
   - `timeout`
   - `fetch failed`

2. Split "Start all channels" into channel groups:
   - realtime trade/BBO/orderbook
   - futures reference data
   - sparse events such as liquidations
   - REST history/candles

3. Add a recorder-level timeout/error reason for connect, subscribe, first-event, and REST fetch phases.

4. Add MEXC futures to the GUI behind a degraded readiness label:
   - live WS: enabled
   - REST candles: disabled/failing until fixed
   - detailed candles selector: disabled until REST kline works

5. Allow `candles2` end-time selection in GUI and CLI, especially for FINAM and other session-based markets.

6. Warn when a capture exits cleanly with zero rows for a required channel.

7. Fix session directory uniqueness for parallel CLI/manual runs.

8. Make unsupported spot-only/futures-only reference channels impossible to start, or visibly label them as not expected.

## Not verified

- Real QML click automation was not performed.
- No private/order routes were exercised.
- No builds, tests, or compilers were run.
- FINAM was not retested during an open market session.
- Gate futures root cause was not fixed; only the timeout behavior was reproduced.
