# l1 / var01_raw_lz4_cpp

**Status:** Phase 1 placeholder. `encode`/`decode` return `Status::Unimplemented`.

## What this variant is

Baseline for the `L1BookTicker` stream family. Serialises each `BookTickerData`
event as a fixed-width row and passes the contiguous block through `lz4`. Fast
online baseline — not the strongest ratio but very cheap to run live.

## Pipeline

```
BookTickerData stream
  → serialize each event as fixed-width row (bid_price, bid_qty, ask_price, ask_qty, ts)
  → concatenate rows into a contiguous byte block
  → lz4 compress (accel = 1; default fast mode)
  → write as BlockHeader{codec=LZ4}.payload
```

## References

1. `doc/FILE_FORMAT.md` — §"Block layout", §"Block flush policy"
2. `doc/CODEC_VARIANTS.md` — row `codec_id = LZ4`
3. `doc/EXTERNAL_LIBRARIES.md` — lz4 link line, frame vs block mode
4. `doc/STREAMS.md` — §"L1 / bookTicker"
5. `doc/COMPARISON_MATRIX.md`

## How to implement (Phase 3)

1. Finish `src/support/external_wrappers/Lz4Wrapper.cpp` (prefer `LZ4_compress_default`
   for block mode — we manage framing via BlockHeader ourselves).
2. Define `BookTickerRow` POD in `row_layout.hpp`. Use int64 scaled 1e8 for
   prices/qty per `doc/CODING_STYLE.md`.
3. `encode.cpp`: serialize rows → `lz4Encode` into caller's `out`.
4. `decode.cpp`: `lz4Decode` → split by `sizeof(BookTickerRow)`.
5. Roundtrip test in `tests/unit/test_l1_var01_roundtrip.cpp`.

## Acceptance gate

- `./run.sh build` → 0
- Roundtrip unit test green
- `kOnlineFeasible = true` is honest: lz4 sustains > 500 MB/s on a single core,
  well above any L1 ingest rate.

## Known pitfalls

- `LZ4_compressBound(n)` gives the worst-case output size; size your `out` to that.
- Do not use LZ4 "frame" mode here — framing duplicates what BlockHeader already
  provides and wastes 7 bytes per block.
