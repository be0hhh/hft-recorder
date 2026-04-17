# orderbook / var01_raw_updates_cpp

**Status:** Phase 1 placeholder. `encode`/`decode` return `Status::Unimplemented`.

## What this variant is

Null baseline for the `OrderbookUpdates` stream family. Records diff-book
updates as-arrived with **no transformation and no compression** — the block
payload is simply the concatenation of serialized updates. Exists so other
orderbook variants can be compared against the "do nothing" floor.

Because ratio = 1.0, this variant is expected to lose on compression. It wins
on encode/decode latency — and that is the whole point of having it.

## Pipeline

```
OrderBookDeltaEvent stream (first_update_id, final_update_id, is_bid,
                            price_count, [(price, qty) × price_count])
  → serialize each event with a length-prefix header and raw (price, qty) pairs
  → concatenate into contiguous bytes
  → no codec
  → write as BlockHeader{codec=NONE_RAW}.payload
```

## References

1. `doc/FILE_FORMAT.md` — §"Block layout", §"CODER_RESET"
2. `doc/DELTA_ENCODING.md` — §"OrderBook (depth@0ms)" (semantics of amount=0 =
   "delete level", which is the `GAP_MARKER` even in the raw variant)
3. `doc/STREAMS.md` — §"depth@0ms"
4. `doc/ORDERBOOK_REPRESENTATION_EXPERIMENTS.md` — this is the anchor against
   which the representation experiments are scored
5. `doc/CXETCPP_USAGE_EXAMPLES.md` — §`runSubscribeOrderBookDeltaByConfig`

## How to implement (Phase 3)

1. Define `OrderBookUpdateHeader` + `LevelEntry` PODs in `row_layout.hpp`.
2. `encode.cpp`: walk the event stream, emit header then raw `(price, qty)` pairs.
3. `decode.cpp`: inverse.
4. Respect the `CODER_RESET` flag: at a reset, any decoder state (none in this
   variant) is cleared. For this raw variant that is a no-op — but the flag
   must still survive serialization round-trip.
5. Roundtrip test in `tests/unit/test_orderbook_var01_roundtrip.cpp`.

## Acceptance gate

- `./run.sh build` → 0
- Roundtrip unit test green
- `compression_ratio = 1.0` ± epsilon (expected)
- `kOnlineFeasible = true` (memcpy-only pipeline)

## Known pitfalls

- `OrderBookDeltaEvent` is **variable length** (N levels per event). Do not
  assume a fixed row size here — emit a per-event length prefix.
- Do not conflate this with `OrderBookSnapshot` (full book via REST). Snapshots
  are keyframes inside orderbook blocks, not a separate stream.
