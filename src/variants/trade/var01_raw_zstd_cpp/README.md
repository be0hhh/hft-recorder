# trade / var01_raw_zstd_cpp

**Status:** Phase 1 placeholder. `encode`/`decode` return `Status::Unimplemented`.

## What this variant is

Baseline for the `TradeLike` stream family. Takes the raw byte stream of the
block's events (as produced by `BlockWriter::appendEvent` — no delta, no
ZigZag, no columnar transform) and passes it through `zstd`. Decode reverses.

It exists so every custom trade-family variant has a **lower bound** to beat:
if a clever custom pipeline cannot beat `raw + zstd`, it is rejected.

## Pipeline

```
TradeEvent stream
  → serialize each event as fixed-width row
  → concatenate rows into a contiguous byte block
  → zstd compress (level from WriterConfig; default 3)
  → write as BlockHeader{codec=ZSTD}.payload
```

Decode:
```
payload → zstd decompress → contiguous row block → split by sizeof(row)
  → TradeEvent sequence
```

## References — read in this order before writing code

1. `doc/FILE_FORMAT.md` — §"Block layout", §"Block flush policy", §"CODER_RESET / Error recovery"
2. `doc/CODEC_VARIANTS.md` — row for `codec_id = ZSTD`
3. `doc/EXTERNAL_LIBRARIES.md` — how to link libzstd, recommended levels
4. `doc/STREAMS.md` — §"Trades / aggTrade"
5. `doc/COMPARISON_MATRIX.md` — required per-run metrics

## How to implement (Phase 3)

1. Finish `src/support/external_wrappers/ZstdWrapper.cpp` (`zstdEncode` + `zstdDecode`).
   Link libzstd via `pkg-config --libs libzstd` inside `src/support/CMakeLists.txt`.
2. Define the fixed-width `TradeRow` POD in this directory's `row_layout.hpp`
   (do NOT put it in `src/core/` — that is a Phase 5 promotion decision).
3. `encode.cpp`: serialize `TradeRow` array → call `zstdEncode` into the caller's
   `out` buffer. On `Status::OutOfRange`, bail to caller.
4. `decode.cpp`: call `zstdDecode` → split output by `sizeof(TradeRow)`.
5. Add `tests/unit/test_trade_var01_roundtrip.cpp`: build 10 000 random rows,
   encode, decode, compare bit-exact.

## Acceptance gate

- `./run.sh build` → 0
- Roundtrip unit test green
- `doc/COMPARISON_MATRIX.md` fields populated: `input_bytes`, `output_bytes`,
  `compression_ratio`, `encode_ns`, `decode_ns`, `online_feasible = true`
- `metadata.hpp` declares `kOnlineFeasible = true` (zstd lvl 3 is fine online)

## Known pitfalls

- Do **not** include `<string>` or `<vector>` in `encode.hpp` / `decode.hpp`
  (hot path — see `doc/CODING_STYLE.md`).
- Zstd's `ZSTD_compressBound(n)` is the correct upper bound for the out buffer.
  Returning `Status::OutOfRange` when `out_cap < bound` is allowed.
- Output of this pipeline is **not self-describing** — the decoder relies on
  `BlockHeader.event_count` to know how many rows to expect.
