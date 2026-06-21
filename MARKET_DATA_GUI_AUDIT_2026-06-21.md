# hft-recorder market data GUI audit - 2026-06-21

## Scope

Selected roles:
- primary: `recorder_corpus_engineer`
- secondary: `exchange_normalizer_engineer`, `architect`

Subagents:
- none

Assumptions:
- Public market data only. No private/order routes were exercised.
- Runtime checks used the existing `apps/hft-recorder/build/bin/hft-recorder capture` CLI over the recorder capture core and writers. I did not automate real QML clicks.
- No build, compiler, Git, or test command was run for this audit.
- The live window was 60 seconds per public stream. Zero rows can mean unsupported route, quiet market, market closed, parser/subscription issue, or sparse event absence; this audit separates zero rows from process failure.
- Audit date was Sunday, 2026-06-21. FINAM/MOEX live streams and same-window candles are expected to be affected by the closed market.

Latency/safety risks:
- No trading routes were touched.
- CLI probes are runtime network probes, not builds/tests.

Artifacts:
- Main audit root: `/tmp/cxet-recorder-audit-20260621-004225`
- Status files: `capture-status.tsv`, `candles-status.tsv`, `candles2-status.tsv`, `fail-timeout-summary.tsv`
- Aggregates: `live-summary.tsv`, `candles-summary.tsv`, `candles2-summary.tsv`
- Static snapshots: `static-gui-venues.txt`, `static-detailed-candles-gate.txt`, `static-open-interest.txt`
- Raw per-run outputs: `/tmp/cxet-recorder-audit-20260621-004225/runs/**/{stdout.log,stderr.log}`

## Static GUI matrix

GUI-visible venues came from `src/gui/viewmodels/CaptureViewModelRequests.cpp` at audit time:

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
| `mexc_futures` | mexc | futures |

Notes:
- `mexc_futures` is now exposed in the GUI venue matrix.
- Detailed candles still allow MEXC spot only. `supportsDetailedCandlesVenue()` returns true for `mexc/spot` and false for `mexc/futures`.
- `startOpenInterest()` still returns false with status text `OpenInterest display is disabled; no data file was created`.

## Runtime method

Live streams used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  <channel> 60 "$AUDIT_ROOT/runs/<run-id>" <exchange> <symbol> <market>
```

Ordinary candles used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  candles 1 "$AUDIT_ROOT/runs/<run-id>" <exchange> <symbol> <market>
```

Detailed candles used:

```bash
apps/hft-recorder/build/bin/hft-recorder capture \
  --env apps/hft-recorder/.env --api-slot 1 \
  --timeframe 1m --limit 300 \
  candles2 1 "$AUDIT_ROOT/runs/<run-id>" <exchange> <symbol> <market>
```

The run used isolated output directories per channel/venue/symbol to avoid session-path collisions.

Symbols:
- Crypto futures: `BTCUSDT`, except `kucoin futures = XBTUSDTM`, `gate futures = BTC_USDT`, `okx futures = BTC-USDT-SWAP`, `mexc futures = BTC_USDT`.
- Crypto spot: `BTCUSDT`, except `kucoin/okx spot = BTC-USDT`, `gate spot = BTC_USDT`.
- FINAM spot: `SBER@MISX`.
- FINAM futures: `SRU6@RTSX`.

## Saved formats

Recorder output layout is session based:

```text
<run-dir>/<epoch>_<exchange>_<market>_<symbol>/
  manifest.json
  jsonl/*.jsonl
  reports/integrity_report.json
```

Canonical paths:

| Channel | Path | Manifest row schema expected/observed |
|---|---|---|
| trades | `jsonl/trades.jsonl` | `cxet_trade_strict_v1` |
| bookticker | `jsonl/bookticker.jsonl` | `cxet_bookticker_strict_v1` |
| orderbook | `jsonl/depth_tape.jsonl` + `jsonl/depth_sidecar.jsonl` | `cxet_orderbook_tape_rle_sidecar_v1` |
| liquidations | `jsonl/liquidations.jsonl` | `cxet_liquidation_alias_first_v1` |
| candles | `jsonl/candles.jsonl` | `cxet_candle_lite_tiered_v1` |
| candles2 | `jsonl/candles2_1m.jsonl` | `cxet_ohlcv_numeric_v3` |
| mark_price | `jsonl/mark_price.jsonl` | `cxet_mark_price_ref_v1` |
| index_price | `jsonl/index_price.jsonl` | `cxet_index_price_ref_v1` |
| funding | `jsonl/funding.jsonl` | `cxet_funding_ref_dedup_v1` |
| price_limit | `jsonl/price_limit.jsonl` | `cxet_price_limit_ref_v1` |

Observed sample row shapes:
- `candles`: `[tier, tsNs, highE8, lowE8, quoteAmountE8]`
- `candles2_1m`: `[tier, tsNs, openE8, highE8, lowE8, closeE8, volumeE8, quoteAmountE8]`

MEXC spot samples:

```text
candles:  [1,1781980500000000000,6385902000000,6383615000000,6520795000000]
candles2: [1,1781974560000000000,6400645000000,6400845000000,6400000000000,6400000000000,621702820,39791329000000]
```

Orderbook tape and sidecar row counts matched in all successful orderbook probes. `orderbook-sidecar-mismatches.tsv` was empty.

## Live stream results

Legend:
- Numeric cells are rows written in 60 seconds.
- `0` means process exited cleanly but wrote no rows.
- Gate futures rows are `0` here because every live channel timed out and wrote no rows; see failure summary.

| Venue | trades | bookticker | orderbook | liquidations | mark | index | funding | price_limit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| aster futures BTCUSDT | 89 | 831 | 1166 | 0 | 59 | 59 | 2 | 0 |
| aster spot BTCUSDT | 1 | 125 | 1169 | 0 | 0 | 0 | 0 | 0 |
| binance futures BTCUSDT | 16801 | 8591 | 2168 | 1 | 59 | 58 | 1 | 0 |
| binance spot BTCUSDT | 15370 | 8166 | 2204 | 0 | 0 | 0 | 0 | 0 |
| bitget futures BTCUSDT | 705 | 4243 | 2216 | 0 | 154 | 148 | 1 | 0 |
| bitget spot BTCUSDT | 253 | 2080 | 1090 | 0 | 0 | 0 | 0 | 0 |
| bybit futures BTCUSDT | 1056 | 857 | 2549 | 0 | 39 | 87 | 1 | 27 |
| bybit spot BTCUSDT | 515 | 1185 | 2020 | 0 | 0 | 0 | 0 | 31 |
| finam futures SRU6@RTSX | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| finam spot SBER@MISX | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| gate futures BTC_USDT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| gate spot BTC_USDT | 114 | 13 | 10 | 0 | 0 | 0 | 0 | 0 |
| kucoin futures XBTUSDTM | 197 | 2795 | 30002 | 0 | 37 | 34 | 1 | 0 |
| kucoin spot BTC-USDT | 65 | 1115 | 10109 | 0 | 0 | 0 | 0 | 0 |
| mexc futures BTC_USDT | 432 | 23 | 269 | 0 | 40 | 45 | 1 | 0 |
| mexc spot BTCUSDT | 201 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| okx futures BTC-USDT-SWAP | 1810 | 1197 | 921 | 0 | 290 | 212 | 3 | 91 |
| okx spot BTC-USDT | 302 | 607 | 756 | 0 | 0 | 0 | 0 | 95 |

Live status totals:
- `ok`: 136
- `timeout`: 8
- zero-row successful live cells: 75

## Candles results

Legend:
- Numeric cells are rows written.
- `0` with failure details is listed in the failure summary.

| Venue | candles | candles2 1m |
|---|---:|---:|
| aster futures BTCUSDT | 1536 | 300 |
| aster spot BTCUSDT | 1302 | 300 |
| binance futures BTCUSDT | 1536 | 300 |
| binance spot BTCUSDT | 1536 | 300 |
| bitget futures BTCUSDT | 1536 | 300 |
| bitget spot BTCUSDT | 1536 | 300 |
| bybit futures BTCUSDT | 1536 | 300 |
| bybit spot BTCUSDT | 1536 | 300 |
| finam futures SRU6@RTSX | 0 | 0 |
| finam spot SBER@MISX | 0 | 0 |
| gate futures BTC_USDT | 0 | 0 |
| gate spot BTC_USDT | 1536 | 300 |
| kucoin futures XBTUSDTM | 1536 | 195 |
| kucoin spot BTC-USDT | 1536 | 300 |
| mexc futures BTC_USDT | 0 | 0 |
| mexc spot BTCUSDT | 585 | 300 |
| okx futures BTC-USDT-SWAP | 1536 | 300 |
| okx spot BTC-USDT | 1536 | 300 |

Status totals:
- `candles`: 14 ok, 3 fail, 1 timeout.
- `candles2`: 14 ok, 4 fail.

Important error text:
- MEXC futures `candles`: `capture start failed: candles: trader history fetch failed (m1=FetchFailed count=0, m15=NotRequested count=0, d1=NotRequested count=0)`.
- MEXC futures `candles2`: `capture start failed: candles2: trader OHLCV fetch failed exchange=mexc market=futures symbol=BTC_USDT timeframe=1m reason=klines fetch failed request=/api/v1/contract/kline/BTC_USDT?interval=Min1&end=1781992557 response_bytes=0`.
- FINAM futures `candles2`: `request=/v1/instruments/SRU6@RTSX/bars?timeframe=TIME_FRAME_M1&interval.start_time=2026-06-20T16:55:57Z&interval.end_time=2026-06-20T21:55:57Z response_bytes=0`.
- FINAM spot `candles2`: `request=/v1/instruments/SBER@MISX/bars?timeframe=TIME_FRAME_M1&interval.start_time=2026-06-20T16:55:57Z&interval.end_time=2026-06-20T21:55:57Z response_bytes=0`.
- Gate futures live channels timed out across all tested public paths. Gate futures `candles` timed out; `candles2` failed with zero rows.

## Findings

1. MEXC futures is now GUI-visible and live WS works.
   - Current GUI matrix includes `mexc_futures`.
   - Runtime rows were observed for MEXC futures `trades`, `bookticker`, `orderbook`, `mark_price`, `index_price`, and `funding`.
   - Recommendation: keep MEXC futures visible for live WS channels, but label REST candles as unavailable/failing.

2. MEXC futures REST candles are still not ready.
   - Both ordinary `candles` and forced `candles2` failed.
   - The detailed request path was `/api/v1/contract/kline/BTC_USDT?interval=Min1&end=1781992557` with `response_bytes=0`.
   - Current GUI behavior that excludes MEXC futures from detailed candles is justified by this runtime evidence.

3. MEXC spot improved for trades but not for BBO/orderbook in this run.
   - `trades` wrote 201 rows.
   - `bookticker` and `orderbook` exited cleanly but wrote zero rows.
   - REST `candles` and `candles2` worked.

4. Gate futures remains the largest runtime blocker.
   - Every live channel timed out.
   - Ordinary `candles` timed out; `candles2` failed.
   - Gate spot still works for live core channels and both candle paths.

5. FINAM remains inconclusive for live streams on this date and failing for current-window candles.
   - The run happened on Sunday, 2026-06-21.
   - Live streams wrote no rows for `SBER@MISX` and `SRU6@RTSX`.
   - `candles2` used a Saturday/Sunday window and failed with zero response bytes.
   - Recommendation: retest FINAM during an open market window or with explicit historical end time support.

6. Liquidations are only weakly proven.
   - Binance futures wrote 1 liquidation row in this 60 second run.
   - Most other venues wrote zero rows. Sparse event absence should not be treated as parser failure by itself.

7. `price_limit` remains venue-specific.
   - Rows were observed on Bybit spot/futures and OKX spot/futures.
   - Other venues wrote zero rows.

8. Session path collision was avoided in this run.
   - The audit runner used a distinct output directory per channel/venue/symbol.
   - This avoids the collision seen in the 2026-06-20 audit, but does not remove the underlying recorder session-ID uniqueness concern for parallel same-directory manual runs.

9. OpenInterest is still not a startable recorder output.
   - `startOpenInterest()` still returns false.
   - It should stay hidden or explicitly marked as "not implemented in recorder" until wired.

## What works

Runtime-proven in this audit:
- Binance futures and spot: core live streams and both candle modes.
- Bybit futures and spot: core live streams and both candle modes.
- KuCoin futures and spot: core live streams and both candle modes; KuCoin futures `candles2` returned 195 rows for a 300-row request.
- Gate spot: core live streams and both candle modes.
- Bitget futures and spot: core live streams and both candle modes.
- Aster futures and spot: core live streams and both candle modes; spot trades were sparse but non-zero.
- OKX futures and spot: core live streams and both candle modes.
- MEXC spot: trades plus REST `candles` and `candles2`.
- MEXC futures: live WS public streams for trades/bookticker/orderbook/mark/index/funding.

Not runtime-proven or failing:
- Gate futures: timeout/fail across tested public paths.
- MEXC futures REST `candles`/`candles2`: failing.
- MEXC spot `bookticker` and `orderbook`: clean exit with zero rows in this 60 second window.
- FINAM live/candles on the weekend/current-window test: inconclusive/failing.
- OpenInterest: disabled in recorder GUI.

## Suggested QoL backlog

1. Add per-venue/channel readiness badges in Capture UI:
   - `ready`
   - `zero rows`
   - `unsupported`
   - `timeout`
   - `fetch failed`

2. Keep MEXC futures enabled for live WS channels but keep detailed candles disabled until REST klines are fixed.

3. Add a visible recorder-level timeout/error phase:
   - connect
   - subscribe
   - first event
   - REST fetch

4. Add `candles2` explicit end-time selection in GUI and CLI, especially for FINAM/session-based markets.

5. Warn when a capture exits cleanly with zero rows for a required channel.

6. Make session directory uniqueness robust for parallel same-directory CLI/manual runs.

7. Keep OpenInterest hidden or labelled as not implemented until recorder writes a real file.

## Not verified

- Real QML click automation was not performed.
- No private/order routes were exercised.
- No builds, tests, compilers, or Git commands were run.
- FINAM was not retested during an open market session.
- Gate futures root cause was not fixed; only the timeout/failure behavior was reproduced.
