# hft-recorder — Encode Walkthrough (one trade, end to end)

Concrete byte-level trace of a single `TradePublic` event from
`CxetStream::tryPop(TradePublic&)` to `pwrite(fd, ...)`. Every step lists the exact integer
values and the hex bytes produced, so a programmer can `printf("%02x ...")` at any stage and
compare to this document.

All sizes are decimal unless hex-prefixed. Byte order: little-endian throughout (x86_64 native
+ on-disk format). All multi-byte primitives on disk are `#pragma pack(1)` with LE layout —
see `FILE_FORMAT.md`.

---

## 1. The input event

Producer thread (`producer:trades`, pinned to CPU 2) calls `CxetStream<TradePublic,2048>::
tryPop(ev)` and receives:

```cpp
TradePublic ev {
    price     = 3'000'100'000'000          // 30001.00 USDT × 1e8  (Price = int64)
    qty       = 10'000'000                 // 0.10 BTC × 1e8       (Amount = int64)
    timestamp = 1'713'168'000'000'000'000  // ns since epoch       (TimeNs = int64)
    id        = 5'123'456                  // Binance aggTrade id   (uint64)
    side      = 1                          // 1 = buy, 0 = sell    (uint8)
    symbol    = "BTCUSDT"                  // fixed 16-byte Symbol buffer
};
```

Writer-local state **before** this event (carried over from the previous appended event):

```cpp
TradeDeltaState state {
    lastPrice = 3'000'000'000'000,
    lastTs    = 1'713'167'999'998'000'000,    // 2 ms earlier
    lastId    = 5'123'455,
    isBlockStart = false,                     // 42nd event of the current block
};
```

If `isBlockStart == true`, the encoder writes **absolute** values for `ts`, `id`, `price` and
updates state directly; the rest of this doc traces the common delta case.

---

## 2. Delta compute

Pseudocode inside `TradeDeltaEncoder::appendEvent(ev, out, &written)`:

```cpp
int64_t dp  = ev.price     - state.lastPrice;      // = 100'000'000'000
int64_t dts = ev.timestamp - state.lastTs;         // = 2'000'000   (2 ms in ns)
int64_t did = int64_t(ev.id) - int64_t(state.lastId);   // = 1
int64_t qtyRaw = ev.qty;                           // = 10'000'000 (not delta'd)
uint8_t sideBit = uint8_t(ev.side);                // = 1
```

Values:

| Field | Decimal | Hex (int64) |
|---|---:|---:|
| dp | 100'000'000'000 | `0x0000'0000'1748'76B8'000` → 0x174876B800 |
| dts | 2'000'000 | 0x1E8480 |
| did | 1 | 0x01 |
| qtyRaw | 10'000'000 | 0x989680 |
| sideBit | 1 | 0x01 |

Clock skew guard: `if (dts < 0) { metrics::clockSkew(stream).increment(); dts = 0; }` — not
triggered here.

---

## 3. ZigZag encode (signed → unsigned)

```cpp
uint64_t zigzag(int64_t n) { return (uint64_t(n) << 1) ^ uint64_t(n >> 63); }
```

Positive inputs only in this trace → `zz(n) = 2n`.

| Field | zz value | Hex |
|---|---:|---:|
| zz(dp) | 200'000'000'000 | 0x2E90EDD000 |
| zz(dts) | 4'000'000 | 0x3D0900 |
| zz(did) | 2 | 0x02 |
| qtyRaw | 10'000'000 (unchanged — not delta'd, not zz'd) | 0x989680 |
| sideBit | 1 (unchanged) | 0x01 |

---

## 4. VarInt encode

7-bit groups, LSB first. Each non-terminal byte has MSB set; the last byte has MSB clear.

```cpp
void encodeVarint(uint64_t v, uint8_t*& p) {
    while (v >= 0x80) { *p++ = uint8_t(v | 0x80); v >>= 7; }
    *p++ = uint8_t(v);
}
```

Working through each field:

### zz(dts) = 4'000'000 = 0b1111'010000'100100'000'0000

```
iter | v (hex)       | byte emitted
   1 | 0x003D0900    | 0x80  (v&0x7F=0x00 | 0x80)
   2 | 0x00007A12    | 0x92  (0x12 | 0x80)
   3 | 0x000000F4    | 0xF4  (0x74 | 0x80)
   4 | 0x00000001    | 0x01  (0x01, MSB clear → terminal)
```

→ **4 bytes**: `80 92 F4 01`

### zz(did) = 2

Single iteration, `v < 0x80`: → **1 byte**: `02`

### zz(dp) = 200'000'000'000 = 0x2E'90ED'D000

```
iter | v (hex)       | byte emitted
   1 | 0x2E90EDD000  | 0x80
   2 | 0x005D21DBA0  | 0xA0  (0x20 | 0x80)
   3 | 0x000BA43B74  | 0x97  (0x17 | 0x80)  — wait, recompute below
```

Careful derivation (LSB out first):

```
v0 = 200'000'000'000                  ; rem = 0        ; v1 = 1'562'500'000
v1 = 1'562'500'000                    ; rem = 32       ; v2 = 12'207'031
v2 = 12'207'031                       ; rem = 23       ; v3 = 95'367
v3 = 95'367                           ; rem = 7        ; v4 = 745
v4 = 745                              ; rem = 105      ; v5 = 5
v5 = 5                                ; terminal
```

→ bytes: `0x80 0xA0 0x97 0x87 0xE9 0x05`  (6 bytes)

### qtyRaw = 10'000'000

```
10'000'000  → rem 0  → next 78'125
78'125      → rem 45 → next 610
610         → rem 98 → next 4
4           → terminal
```

→ bytes: `0x80 0xAD 0xE2 0x04`  (4 bytes)

### sideBit = 1

Written as a single raw byte in the VARINT baseline (it is one bit when the AC coder
consumes the stream, but the VARINT path has no bit packer — one byte is simpler, costs +7
bits per trade, which VARINT makes up for elsewhere).

→ byte: `0x01`  (1 byte)

---

## 5. Encoding order and concatenation

From `DELTA_ENCODING.md` § "aggTrade" encoding order:

```
[delta_ts_zigzag] [delta_id_zigzag] [delta_price_zigzag] [qty_raw] [side_bit]
```

Concatenated bytes for this event (16 bytes total):

```
80 92 F4 01           ; dts varint (4 bytes)
02                    ; did varint (1 byte)
80 A0 97 87 E9 05     ; dp  varint (6 bytes)
80 AD E2 04           ; qty varint (4 bytes)
01                    ; side         (1 byte)
```

Hex dump, contiguous:

```
80 92 F4 01 02 80 A0 97 87 E9 05 80 AD E2 04 01
```

After this event the encoder updates state:

```cpp
state.lastPrice = ev.price;
state.lastTs    = ev.timestamp;
state.lastId    = ev.id;
state.isBlockStart = false;
```

---

## 6. Append into block buffer

`BlockWriter::appendEventBytes(span{16 bytes})`:

- Block buffer grows by 16 bytes. Post-state:
  - `eventCount = 42`       (was 41, now 42)
  - `blockBytes = 678`      (was 662, now 662 + 16)
  - `wallElapsed = 230 ms`  (since first event of this block)
- Flush policy check (`FILE_FORMAT.md` § "Block flush policy") — all three:
  - `eventCount >= 512`?       → false (42 < 512)
  - `wallElapsed >= 1000 ms`?  → false (230 < 1000)
  - `blockBytes >= 262144`?    → false (678 < 262144)
- No flush; `appendEvent` returns `Status::Ok`.

---

## 7. 470 more events later → flush

Block hits `eventCount == 512`. `BlockWriter::flushBlock(reason = EventCount)`:

Assume final `blockBytes = 7234`, `wallElapsed = 940 ms` (flush triggered by event count first).

### 7.1 Codec layer

Current codec = `Varint`. Trivial path:

```cpp
// VarintEncoder::encode(in, out, *outWritten)
std::memcpy(out.data(), in.data(), in.size());
*outWritten = in.size();           // = 7234
```

Payload bytes = block buffer bytes, unchanged. For AC codecs this is where arithmetic /
rANS transforms the same 7234 bytes into fewer bytes — see § 10 for the expected AC_BIN16_CTX8
outcome.

### 7.2 CRC32C of payload

```cpp
uint32_t crc = crc32c(payload, 7234);   // = say, 0xA1B2C3D4 (depends on data)
```

### 7.3 Build `BlockHeader` (32 bytes)

```
offset  size  field            value              hex
  0      4    magic            kBlockMagic        42 4C 4B 00      ("BLK\0")
  4      2    version          1                  01 00
  6      1    stream_type      kStreamAggTrade    01
  7      1    codec_id         kCodecVarint       01
  8      4    event_count      512                00 02 00 00
 12      4    payload_size     7234               42 1C 00 00
 16      8    first_event_ts   1713168000000000000  00 40 F4 0F 13 EC C7 17
 24      1    flags            0                  00
 25      3    reserved         0                  00 00 00
 28      4    crc32c(payload)  0xA1B2C3D4         D4 C3 B2 A1
```

`flags = 0` here (normal block). If this were the 1024th block since last reset, `flags` would
be `0x01` (`kFlagCoderReset`) and the writer would also reset its delta encoder + AC state.

### 7.4 pwrite

```cpp
// header to current file offset, then payload right after.
pwrite(fd, &header,  sizeof(BlockHeader), offset);       // 32 bytes
pwrite(fd, payload, 7234,                 offset + 32);  // 7234 bytes
offset += 32 + 7234;
```

Metric emissions (see `LOGGING_AND_METRICS.md`):

```
hft_recorder_blocks_flushed_total{stream="trades",reason="event_count"} +1
hft_recorder_bytes_written_total{stream="trades"} +7266
hft_recorder_block_compressed_bytes{stream="trades"} observe 7234
hft_recorder_block_flush_seconds{stream="trades"} observe 0.000123  // includes crc + pwrite
```

Log line:

```
[2026-04-17T15:23:41.123+00:00] [writer.trades] [info] [sym=BTCUSDT ex=binance stream=trades]
  block flushed: events=512 bytes=7234 reason=event_count offset=12582912 crc=0xA1B2C3D4
```

### 7.5 fsync cadence

`blockCountTotal % kFsyncEveryBlocks == 0` (every 16 blocks):

```cpp
fsync(fd);
hft_recorder_fsync_duration_seconds{stream="trades"} observe dt
```

---

## 8. Block recap on disk

Hex dump of the first 64 bytes of this block as it lands on disk:

```
offset     0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F
0000000   42 4C 4B 00 01 00 01 01   00 02 00 00 42 1C 00 00   "BLK\0....\x02..B\x1C.."
0000010   00 40 F4 0F 13 EC C7 17   00 00 00 00 D4 C3 B2 A1   .@......\xD4\xC3\xB2\xA1
0000020   80 92 F4 01 02 80 A0 97   87 E9 05 80 AD E2 04 01   ..............  (event 42 — the one we traced!)
0000030   ...                                                  (next event begins)
```

The trace event starts at on-disk offset `file_offset_of_block + 32 + 41*16` (assuming all 42
events are equal-sized, which they aren't; in reality the offset depends on the actual bytes of
preceding events). This is where a programmer can place a breakpoint + `xxd` the file to verify.

---

## 9. Decoder path (sanity check)

Bench tool replaying the same block:

```
read BlockHeader (32 B)  → magic OK → stream=trades codec=varint events=512 payload=7234 crc=...
read payload (7234 B)    → crc32c(payload) must equal header.crc32c   → if mismatch → skip to next CODER_RESET
VarintDecoder::decode(payload, out, eventCount=512)  → Status::Ok
TradeDeltaDecoder::reset()                            → state = {0,0,0,...}
for i in 0..512:
    read dts  = decodeZigzagVarint(payload)
    read did  = decodeZigzagVarint(payload)
    read dp   = decodeZigzagVarint(payload)
    read qty  = decodeVarint(payload)
    read side = readByte(payload)
    if i == 0 and block.first_event:
        state.lastTs = ...absolute values written by encoder...
    ev.timestamp = state.lastTs   + dts
    ev.id        = state.lastId   + did
    ev.price     = state.lastPrice + dp
    ev.qty       = qty
    ev.side      = side
    state update; emit ev.
```

For event 42 the decoder reconstructs the exact `TradePublic` we started from — this is what
`test_roundtrip_all.cpp` asserts.

---

## 10. Re-encode under AC_BIN16_CTX8 (bench path)

Same 7234-byte block fed to `AcBin16Ctx8Encoder::encode(in, out, *outWritten)`:

1. `reset()` — uniform 256-context probability table (prob₀ = prob₁ = 0.5 → count₀ = count₁ = 1).
2. For each **bit** of each byte of `in`:
   - context = `last_payload_byte` (initially 0).
   - Look up prob₀ from `table[context]`.
   - Narrow `Low, Range` per Subbotin formula (`ARITHMETIC_CODING.md` § Subbotin).
   - Renormalise: emit 0..N bytes.
   - Update `count_{bit}` in `table[context]`; halve when `count₀ + count₁ ≥ kAcMaxCount = 4096`.
3. After the last bit: flush the 64-bit `Low` with carry-propagation, pad the last byte with
   zero bits.
4. `*outWritten` ≈ **4200 bytes** (expected ~42% shrink — see `BENCHMARK_PLAN.md`).

Metric emissions (bench side):

```
hft_recorder_bench_blocks_processed_total{codec="ac_bin16_ctx8",stream="trades"} +1
hft_recorder_bench_ratio{codec="ac_bin16_ctx8",stream="trades"} = 0.581  (4200/7234)
hft_recorder_bench_encode_ns{codec="ac_bin16_ctx8",stream="trades",quantile="0.5"} observe ≈ 240000  (240 μs)
hft_recorder_bench_decode_ns{codec="ac_bin16_ctx8",stream="trades",quantile="0.5"} observe ≈ 260000
```

The whole-file ratio is emitted as `hft_recorder_bench_ratio_whole` at run end.

---

## 11. What a programmer should do with this

- **Writing the VARINT codec?** Compare your encode output to the 16-byte dump in § 5. If it
  differs, your ZigZag or VarInt implementation is wrong; this doc is the reference.
- **Writing the AC codec?** Feed the 7234-byte block from § 7 as input and confirm your output
  is within ±10% of 4200 bytes. A ratio worse than 0.85 means your context table is not
  adapting (check `count` updates and halving).
- **Adding a new stream type?** Use this doc as the template — write the equivalent section
  with dummy data for your stream and commit it. Every stream that ships in the recorder has
  its own walkthrough entry.
- **Debugging round-trip failure?** The first 64 bytes hex-dump in § 8 is the ground truth.
  Binary diff against your encoder's output and walk back to the field that differs.

---

## References

- `DELTA_ENCODING.md` § "aggTrade" — encoding order used in § 5.
- `FILE_FORMAT.md` § "BlockHeader" — offsets used in § 7.3.
- `ARITHMETIC_CODING.md` § "Subbotin carry-propagation algorithm" — the transform in § 10.
- `API_CONTRACTS.md` § `BlockWriter` — `appendEventBytes` / `flushBlock` signatures.
- `LOGGING_AND_METRICS.md` — every metric cited in § 7 and § 10.
- `TESTING_CONTRACT.md` — the test that validates this exact trace round-trip.
