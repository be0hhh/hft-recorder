# hft-recorder — Arithmetic Coding

Theory and implementation guide for the arithmetic coder used in `.cxrec` encoding.

---

## Background: What Is Arithmetic Coding?

Arithmetic coding represents a sequence of symbols as a single rational number in the
interval [0, 1). Each symbol narrows the interval proportionally to its probability:

```
Initial interval: [L=0, R=1)

Symbol s₁ with P(s₁) = 0.7:
  [L=0, R=0.7)

Symbol s₂ with P(s₂|s₁) = 0.4:
  [L=0, R=0.28)   (L + (R-L)*0   to  L + (R-L)*0.4)

Symbol s₃ with P(s₃|s₁,s₂) = 0.9:
  [L=0, R=0.252)
```

The final code is any value inside [L, R), representable in ⌈-log₂(R-L)⌉ bits.
Code length approaches the Shannon entropy: ∑ -log₂(pᵢ) bits.

---

## Binary Arithmetic Coding

We use a **binary** alphabet: symbols are individual bits (0 or 1).
This simplifies the coder significantly: instead of an arbitrary CDF, we need
only one probability per context: `P(bit=1 | context)`.

```
encode_bit(bit, prob1):
    // prob1 = P(bit=1) in [0, 65535] for 16-bit variant
    mid = L + ((R - L) * prob1) >> 16
    if bit == 1:
        L = mid + 1
    else:
        R = mid
    renormalize()

decode_bit(prob1) -> bit:
    mid = L + ((R - L) * prob1) >> 16
    if V >= mid + 1:     // V = current code value
        L = mid + 1
        return 1
    else:
        R = mid
        return 0
    renormalize()
```

Each 64-bit integer encodes ~64 bits of payload. The coder operates on a stream
of (bit, context) pairs produced by the delta encoder.

---

## Renormalization

When the interval [L, R) becomes too narrow, we emit output bits and widen it:

```
renormalize():
    while (R - L) < (1 << (PRECISION - 8)):
        if top bits of L and R agree:
            emit that bit
            L <<= 1
            R <<= 1
            R |= 1
        else if near-carry (underflow risk):
            // bit stuffing or carry propagation
```

For 64-bit precision (`PRECISION = 64`), the threshold is typically `1 << 56`.

### Carry Propagation (64-bit variant)

Using 64-bit `L` and `R` eliminates the classic underflow problem:
the interval can narrow to any positive width without the top bit of L and R
ever "disagreeing" in a way that requires bit stuffing, as long as we use
a sufficiently wide interval representation.

The encoder emits the top byte of `L` when it stabilizes (matches `R`'s top byte).
The decoder reads one byte ahead to fill `V`.

---

## Adaptive Probability Model

The coder uses **adaptive models**: probabilities start uniform and are updated
after each symbol.

### Per-context counters

```cpp
struct Model {
    uint16_t count0[NUM_CONTEXTS];  // count of 0-bits seen in each context
    uint16_t count1[NUM_CONTEXTS];  // count of 1-bits seen
};
```

Probability of bit=1 in context `c`:

```
prob1(c) = (count1[c] + 1) / (count0[c] + count1[c] + 2)   // Laplace smoothing
```

For 16-bit fixed-point arithmetic:

```
prob1_fixed16(c) = ((count1[c] + 1) * 65536) / (count0[c] + count1[c] + 2)
```

### Update rule

```
after encoding/decoding bit b in context c:
    if b == 1: count1[c]++
    else:      count0[c]++
    if (count0[c] + count1[c]) > MAX_COUNT:
        count0[c] >>= 1      // halve to forget old data
        count1[c] >>= 1
```

`MAX_COUNT = 4096` is a reasonable default (tunable per stream type).
Halving preserves the ratio while adapting to distribution shifts (e.g. after a news event).

### Laplace Smoothing

Starting with `count0 = count1 = 0` would give undefined `prob1`.
Adding 1 to each numerator and 2 to the denominator (Laplace / add-one smoothing)
ensures a valid probability even on the first symbol.

---

## Context Selection

The context determines which probability counter to use for each bit.

### Context Composition

```
context = (field_id & 0xF) | (bit_position & 0xF) << 4   // 8-bit context
```

Where:
- `field_id` (4 bits): which field is being encoded (delta_price=0, delta_ts=1, etc.)
- `bit_position` (4 bits): position of this bit within the current VarInt byte (0–7)

For a 12-bit context:

```
context = (field_id & 0xF) | (bit_position & 0xF) << 4 | (recent_byte & 0xF) << 8
```

where `recent_byte` = the last fully-encoded byte value (captures local magnitude).

### Why Context Matters

- Bit 0 (LSB) of `delta_price` has P(1) ≈ 0.5 (random tick direction)
- Bit 6 of `delta_price` has P(1) ≈ 0.02 (price rarely moves > 64 ticks)
- Bit 0 of `delta_ts` for bookTicker has P(1) ≈ 0.85 (timestamps are odd-valued)

Without context, the model must use a single average probability → poor compression.
With context, each bit position gets its own calibrated estimate → near-optimal.

---

## Context Table Sizes

| Variant | Context bits | Table size | Memory |
|---------|-------------|-----------|--------|
| CTX0 | 0 | 1 entry | 4 B |
| CTX8 | 8 | 256 entries | 1 KB |
| CTX12 | 12 | 4096 entries | 16 KB |

CTX12 fits in L1 cache (typically 32–64 KB). CTX8 is always in L1.

---

## Range Coder Variant (no division)

Standard AC requires a division to compute `mid`:

```
mid = L + ((R - L) * prob1) >> 16    // multiply + shift, no division
```

Actually this is already division-free with the `>> 16` shift when `R - L` is
a power of 2. The range coder variant maintains `range = R - L` as a power of 2:

```
range >>= 1                           // always halves the interval
mid = L + range                       // no multiply
if bit == 1: L = mid
else:        R = mid   (= L + range)
```

This eliminates the multiply but requires a 1-bit context (cannot encode
arbitrary probabilities). For multi-symbol or probabilistic contexts, the
standard AC is more flexible. The range coder trades accuracy for speed —
see [CODEC_VARIANTS.md](CODEC_VARIANTS.md) for the tradeoff analysis.

### Subbotin Range Coder

The Subbotin range coder (`rangecod.c`, widely used in 7-Zip and LZMA) avoids
the carry-propagation problem in classic AC by maintaining a `Low` accumulator
as a 64-bit integer and a separate `Range` as 32-bit:

```
state: uint64 Low = 0, uint32 Range = 0xFFFFFFFF
encode(cum, freq, total):
    Low  += cum * (Range /= total)
    Range *= freq
    while Range < (1 << 24):
        emit byte(Low >> 24)
        Low   = (Low & 0x00FFFFFF) << 8
        Range <<= 8
```

The 64-bit `Low` absorbs carry naturally: carry propagates into bits 32+ of `Low`
and is emitted as the next output byte when normalization runs. No special carry
buffering is needed. This is simpler than the pure AC carry-byte approach and
is the recommended implementation base for RANGE_CTX8 and AC_BIN16_CTX8.

---

## rANS (Asymmetric Numeral Systems)

rANS (range Asymmetric Numeral Systems, Jarek Duda 2013) encodes symbols into
a single integer state `x` instead of an interval `[L, R)`. This changes the
performance profile significantly:

### Core Equations

```
// Encoding (push symbol s with frequency f, total M):
encode(x, s):
    x' = (x / f) * M + cumFreq(s) + (x % f)
    emit bytes while x' >= RANS_UPPER_BOUND
    return x'

// Decoding (pop symbol):
decode(x):
    s = symbol_lookup(x % M)       // table lookup
    x' = f(s) * (x / M) + (x % M) - cumFreq(s)
    refill: while x' < RANS_LOWER_BOUND: x' = (x' << 8) | read_byte()
    return (s, x')
```

The key property: **decoding is a single table lookup + refill**, which is
much faster than the interval arithmetic of range coders.

### Why rANS Decodes Faster

| Operation | Range coder | rANS |
|-----------|-------------|------|
| Symbol decode | `cumFreq = (code - Low) / (Range / M)` — division | `s = table[x % M]` — array index |
| State update | 2 multiplies + subtract | 1 multiply + add |
| Renormalization | While loop with byte emit/read | While loop with byte read only |

The division in range coder decode is the bottleneck: integer division has
20–80 cycle latency on modern CPUs. rANS replaces it with a modulo (optimized
to a multiply by inverse) and a table lookup.

### rANS with AVX2 (interleaved streams)

Using 4 or 8 interleaved rANS streams allows decode to proceed SIMD-parallel:

```
// 4 interleaved states, AVX2 decode loop:
decode 4 symbols in parallel using _mm256_* intrinsics
→ ~1430 MB/s measured (jkbonfield/rans_static benchmark)
vs ~70 MB/s for scalar range coder
```

For our use case (backtest replay), this means the decompressor is effectively
free at typical data rates (< 50 MB/s for a single ETHUSDT stream).

### Adaptive rANS (for RANS_CTX8)

rANS with static symbol frequencies is straightforward. Adaptive frequencies
(updated per-symbol like AC) require rebuilding the lookup table after each
update — expensive. The practical solution for RANS_CTX8:

- Use **quasi-adaptive** model: update after each block (512 events), not per-symbol
- Within a block, frequencies are static → standard fast rANS
- Block boundaries trigger model update: recompute cumFreq table from counters

This sacrifices some ratio vs fully adaptive AC but maintains rANS decode speed.
The ratio loss is small for stationary tick data (< 5% vs per-symbol adaptive).

### Comparison: AC vs rANS for tick data

| Property | AC_BIN16_CTX8 | RANS_CTX8 |
|----------|---------------|-----------|
| Adaptation | Per-bit (online) | Per-block (quasi) |
| Encode | ~200–300 MB/s | ~300–400 MB/s |
| Decode | ~200–250 MB/s | ~700 MB/s (scalar), ~1430 MB/s (AVX2) |
| Ratio vs VARINT | ~1.8× | ~1.7× |
| Complexity | Medium | Medium-high (table rebuild) |

**Recommendation**: use AC_BIN16_CTX8 for archival recording (slightly better ratio);
use RANS_CTX8 for backtest replay binary (much faster decode). Both codec_ids are
stored in the `.cxrec` header — a replay tool picks the right decoder automatically.

---

## Encoder Flush

At the end of a block, the encoder must flush the remaining interval:

```
flush():
    emit enough bits to uniquely identify any value in [L, R)
    typically: emit ceil(log2(1/(R-L))) bits derived from L
```

Standard approach: emit the top byte of `L+1` (or `L`) enough times, then
a terminator. The exact sequence is defined by the codec variant.

The decoder knows the number of events (`event_count` in block header) and
stops after decoding that many events, ignoring flush padding.

---

## Decoder Initialization

```
init_decoder(bitstream):
    V = read first 8 bytes of bitstream as uint64
    L = 0
    R = UINT64_MAX
```

`V` tracks the current input value and is updated as bits are consumed.

---

## Implementation Notes

- Use `__uint128_t` for the multiply in `mid = L + ((R-L) * prob1) >> 16`
  to avoid overflow when `R-L` is large and `prob1` is 16-bit.
- Alternatively, scale: ensure `R - L <= 2^48` before the multiply, then
  the 48 × 16 = 64-bit product fits in `uint64_t`.
- The model counters (`count0`, `count1`): plain `uint16_t[256]` or `uint16_t[4096]`
  stack/member arrays — not `std::vector`, no heap alloc.
- Output buffers: `absl::InlinedVector<uint8_t, 4096>` (from `extra/abseil-cpp`)
  instead of `std::vector<uint8_t>` — avoids heap allocation for blocks ≤ 4 KB.
- Separate encoder and decoder into two files (`arith_encoder.hpp`,
  `arith_decoder.hpp`). Decoders are used in backtest replay; encoders only
  in the recorder.
- All logging (errors, model resets, block flushes) via `spdlog` (from `extra/spdlog`),
  not `fprintf(stderr)`.
- Bench binary uses `google-benchmark` (from `extra/google-benchmark`) for
  statistics (mean, stddev, p99) on top of raw RDTSC measurements.

---

## Expected Compression Gains Over VarInt Baseline

| Stream | VarInt bytes/event | AC-CTX8 bytes/event | Ratio |
|--------|-------------------|---------------------|-------|
| aggTrade | ~20 | ~12–14 | 1.4–1.7× |
| depth@100ms per level | ~8 | ~4–6 | 1.4–2.0× |
| bookTicker | ~18 | ~8–10 | 1.8–2.2× |

These are estimates based on Shannon entropy analysis of ETHUSDT tick data.
Actual gains depend on market regime (trending vs. ranging vs. volatile).
Measure with [BENCHMARK_PLAN.md](BENCHMARK_PLAN.md).

---

## References

- [DELTA_ENCODING.md](DELTA_ENCODING.md) — what the AC receives as input
- [CODEC_VARIANTS.md](CODEC_VARIANTS.md) — 7 concrete variants with pseudocode
- [BENCHMARK_PLAN.md](BENCHMARK_PLAN.md) — how to measure and compare variants
