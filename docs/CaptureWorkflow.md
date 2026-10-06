# hft-recorder — Parser capture workflow

## User workflow

1. Choose the Parser template and its existing adjacent `.env`.
2. Select exchanges, products, symbols and BBO/trade/depth streams for one batch.
3. Choose duration, physical byte limit and an output directory, normally under
   `/mnt/d/recordings`.
4. Start capture. Recorder starts one owned Parser for the batch; Parser owns
   subscriptions, normalization and complete application-message receive clocks.
5. Recorder writes losslessly compressed binary blocks directly to the corpus.
   It creates no raw or JSONL intermediate file.
6. Supported selection changes go through Parser and are recorded as lifecycle
   boundaries. Other selected lanes keep running; unsupported changes are refused.
7. Stop, duration or quota ends admission and finalizes committed data within
   the reserved finalization budget. Inspect the recorded stop reason and loss evidence.
8. Automatic duration/quota/error termination retains the owned Parser.
   Use `Finalize Session` to stop it before starting another capture. Closing
   Recorder also stops its own child; it does not stop unrelated Parser processes.
9. Run Backtest on the finalized corpus. Replay decodes checked blocks into
   bounded RAM and creates no unpacked file.

## Selection limits

Dynamic changes currently require isolated WS BBO/trade lanes. Appending a new
source requires an already active product in standalone one-shard capture.
Shared/all-market lanes, depth mutations and an empty startup product refuse
dynamic changes explicitly. Historical candle capture remains a separate cold action.

## Error handling

Config, attach, ring, quota and disk failures are explicit in capture status and
corpus evidence. A finalized recording with capture loss cannot be admitted as
exact replay. Native health gaps suspend affected replay contexts and retain
their orders and positions; they do not authorize fabricated prices or fills.

Captured receive times are realtime and monotonic nanoseconds at the accepted
complete application-message boundary. They are not kernel/NIC receive times.
Backtest uses captured arrival without adding a market-data ping; execution and
private-delivery delays remain separate.
