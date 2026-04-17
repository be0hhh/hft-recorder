# hft-recorder — Delta Encoding

Specification of the delta-encoding layer that runs **before** arithmetic coding.
Delta encoding exploits temporal correlation in market data to produce small integers
that the arithmetic coder can compress efficiently.

---

## Why Delta Before Arithmetic Coding?

Raw `Price` and `TimeNs` values are 64-bit integers with high absolute magnitude
(e.g. price = 3 000 000 000 000, timestamp = 1 713 168 000 000 000 000).
These have high entropy when viewed as raw bits.

After delta encoding:
- `delta(price)` is typically ±1 to ±50 ticks → fits in 7–14 bits
- `delta(ts)` for aggTrade is typically 0–50 ms → fits in 16–26 bits
- Side bit alternates or stays constant → near-zero entropy

The arithmetic coder then models the *remaining* entropy in these small values.
The combination is much more effective than either technique alone.

---

## ZigZag Encoding

Signed deltas are mapped to unsigned integers before entropy coding:

```
zigzag_encode(n):
    return (n << 1) ^ (n >> 63)    // n is int64

zigzag_decode(u):
    return (u >> 1) ^ -(u & 1)     // u is uint64
```

This maps { 0, -1, 1, -2, 2, -3, 3, ... } → { 0, 1, 2, 3, 4, 5, 6, ... },
ensuring small-magnitude signed values have small unsigned values, which have
shorter representations in both VarInt and arithmetic coding.

---

## Per-Stream Delta Strategy

### Stream: aggTrade (`TradePublic`)

State carried between events: `{last_price, last_ts, last_id}`

| Field | Encoding | Typical range (ZZ-encoded) | Notes |
|-------|----------|--------------------------|-------|
| `delta_ts` | ZigZag VarInt | 0–5 000 000 (0–5 ms in ns) | Always positive for well-ordered feed |
| `delta_price` | ZigZag VarInt | 0–100 (±50 ticks) | 99th pct; rare spikes up to ±500 |
| `delta_id` | ZigZag VarInt | 1–5 | Usually 1; burst = 2–5 |
| `qty` | VarInt (raw) | varies | Not delta'd — quantities have no autocorrelation |
| `side` | 1 bit raw | 0 or 1 | No delta; alternation is modeled by AC context |

#### Encoding order per event

```
[delta_ts_zigzag] [delta_id_zigzag] [delta_price_zigzag] [qty_raw] [side_bit]
```

All values encoded as VarInt (7-bit groups, MSB = continuation bit) before
passing to the arithmetic coder bit-by-bit.

---

### Stream: depth@0ms (`OrderBookDelta`, raw diff book updates)

State carried between events: `{last_updateId, last_ts, bid_levels{}, ask_levels{}}`
where `bid_levels` and `ask_levels` are price→qty maps maintained by the encoder
to compute price deltas relative to the nearest existing level (sorted order).

Binance fapi emits depth diffs as fast as the book changes (typically
10–20 events per second per symbol, no artificial 100 ms throttling).

| Field | Encoding | Typical range | Notes |
|-------|----------|--------------|-------|
| `delta_updateId` | ZigZag VarInt | 1–3 | Usually 1 per diff message; gap → GAP_MARKER block, state reset |
| `delta_ts` | ZigZag VarInt | 0–200 000 000 ns | Large variance — raw diff, not throttled |
| `bid_count` | uint8 raw | 0–20 | Number of bid level changes in this event |
| `ask_count` | uint8 raw | 0–20 | |
| `bid[i].delta_price` | ZigZag VarInt vs nearest | ±1–±20 ticks | Sorted, delta from previous level |
| `bid[i].qty` | VarInt raw | varies | `qty = 0` means "delete level" (Binance semantics) |
| `ask[i].delta_price` | ZigZag VarInt | ±1–±20 ticks | |
| `ask[i].qty` | VarInt raw | varies | `qty = 0` = delete |

#### Block-start absolute values

The **first event in each block** (and the first event after a `CODER_RESET`
block) writes **absolute** values for `updateId`, `ts`, and every level's
`price` and `qty` — not deltas. Subsequent events in the same block use
the delta forms from the table above.

```
on block_start or coder_reset:
    state = {last_updateId: 0, last_ts: 0, bid_levels: {}, ask_levels: {}}
    write absolute updateId, ts, per-level (price, qty)
on subsequent event in same block:
    write delta_updateId, delta_ts, per-level (delta_price, qty)
```

This rule ensures every block is a self-contained random-access unit — a
decoder can begin at any `CODER_RESET` or block boundary without carrying
state from earlier in the file.

#### Level price delta scheme

Bid levels are sorted descending; ask levels ascending. Within an event,
price deltas are computed relative to the **previous level in the same event**
(first level: delta from the previous event's top level in the same side, or
absolute on block start).

```
batch_bids = sort(incoming_bids, descending)
delta_price[0] = zigzag(batch_bids[0].price - state.last_bid_top)
delta_price[i] = zigzag(batch_bids[i].price - batch_bids[i-1].price)  // i > 0
```

This keeps deltas small even when multiple levels change simultaneously.

#### GAP_MARKER (level deletion)

Binance uses `qty = 0` on a diff level to mean "remove this level from the
book". The encoder treats this as a natural gap marker: when a level that
existed in `state.bid_levels` is absent from the current event, the encoder
emits a record with `delta_price` relative to that stale level and `qty = 0`.
The decoder recognizes `qty == 0` as the deletion signal and removes the
level from its reconstructed book.

> Rationale (WHY): `qty == 0` is the exchange-native delete encoding; reusing
> it as our GAP_MARKER costs zero extra bits and stays byte-for-byte identical
> to the upstream semantics, so round-trip against captured raw payloads is
> trivially auditable.

---

### Stream: bookTicker (`BookTicker`)

State: `{last_updateId, last_bidPrice, last_askPrice, last_ts}`

| Field | Encoding | Typical range | Notes |
|-------|----------|--------------|-------|
| `delta_updateId` | ZigZag VarInt | 1–10 | |
| `delta_ts` | ZigZag VarInt | 0–1 000 000 ns (0–1 ms) | Very high frequency |
| `delta_bidPrice` | ZigZag VarInt | ±0–±5 ticks | Usually 0 or ±1 |
| `delta_askPrice` | ZigZag VarInt | ±0–±5 ticks | |
| `bidQty` | VarInt raw | varies | Quantities not delta'd |
| `askQty` | VarInt raw | varies | |

BookTicker is the most compressible stream: `delta_bidPrice` and `delta_askPrice`
are 0 in ~60% of events (only quantity changed), giving the AC model very low entropy.

---

### Stream: DEPTH_SNAPSHOT (REST)

Snapshot is encoded as a single block. No delta encoding relative to previous events
(it's a one-shot full state dump). Prices within the snapshot are delta-encoded
**relative to the previous level** (sorted order), same as the `depth@0ms` per-event scheme:

```
bids sorted descending:
delta_price[0] = zigzag(bids[0].price)           // delta from zero
delta_price[i] = zigzag(bids[i].price - bids[i-1].price)
```

This compresses well because order book levels are closely spaced.

---

### Stream: fundingRate

Excluded from MVP. `stream_type` range `0x05..0xFF` is reserved in the file
format; if added later, follow the same per-field delta template used above.

---

## Context Byte Synchronization

The arithmetic-coder context (see [ARITHMETIC_CODING.md](ARITHMETIC_CODING.md))
for `CTX8` / `CTX12` depends in part on a "recent byte" of the output payload.
For encoder and decoder to compute the same context, both must maintain an
identical `last_payload_byte` state automaton:

```
initial state on block_start / coder_reset:
    last_payload_byte = 0x00

encoder: after emitting each full VarInt byte of the delta-encoded stream:
    last_payload_byte = that byte

decoder: after reconstructing each full VarInt byte:
    last_payload_byte = that byte
```

The context is taken from `last_payload_byte`, **not** from the raw `int64`
delta values — this keeps the context synchronous even when the underlying
numeric deltas overflow intermediate integer types, and avoids the decoder
needing to reconstruct the arithmetic-decoded integer before it knows the
next context.

> Rationale (WHY): fixing the context to an already-emitted output byte makes
> encoder/decoder sync trivial. If context depended on the still-being-decoded
> integer, the decoder would need to speculatively buffer bits before it knew
> which probability to use — a classic chicken-and-egg problem.

---

## VarInt Format

Standard 7-bit continuation encoding, little-endian:

```
encode_varint(v: uint64):
    while v >= 0x80:
        emit((v & 0x7F) | 0x80)
        v >>= 7
    emit(v & 0x7F)
```

A value of 0–127 encodes in 1 byte; 128–16383 in 2 bytes; etc.

When used as input to the arithmetic coder, each byte of the VarInt is fed bit-by-bit
(LSB first within each byte, bytes in order). The AC model conditions on recent bits
and the field type to predict each incoming bit.

---

## Estimated Delta Ranges Summary

| Stream | Field | 90th pct delta | 99th pct delta | Bits needed (90th) |
|--------|-------|----------------|----------------|-------------------|
| aggTrade | delta_price | ±3 ticks | ±30 ticks | 4 bits ZZ |
| aggTrade | delta_ts | 2 ms | 20 ms | 21 bits |
| aggTrade | delta_id | 1 | 3 | 2 bits ZZ |
| depth@0ms | delta_ts | 50 ms | 200 ms | 26 bits |
| depth@0ms | level delta_price | ±5 ticks | ±50 ticks | 5 bits ZZ |
| bookTicker | delta_bidPrice | 0 ticks | ±3 ticks | 1 bit ZZ |
| bookTicker | delta_ts | 0.2 ms | 2 ms | 18 bits |

(Tick size for ETHUSDT futures = 0.01 USDT = 1 000 000 in Price units.)

---

## References

- [ARITHMETIC_CODING.md](ARITHMETIC_CODING.md) — what happens to the ZigZag VarInt bits
- [CODEC_VARIANTS.md](CODEC_VARIANTS.md) — which codec variant uses which encoding
- [STREAMS.md](STREAMS.md) — source field types
- [FILE_FORMAT.md](FILE_FORMAT.md) — how encoded data is stored in blocks
