# hft-recorder — .cxrec File Format

Binary format specification for compressed market data log files.

---

## Design Goals

- **Self-describing**: every file carries enough metadata to decode without external schema
- **Streamable**: can be read sequentially without seeking (no index required)
- **Restartable**: corruption in one block does not prevent reading subsequent blocks
- **Appendable**: new blocks written atomically (block header written last)

---

## File Structure

```
┌─────────────────────────────────────┐
│          FILE HEADER (64 bytes)     │
├─────────────────────────────────────┤
│          BLOCK 0 (variable size)    │
├─────────────────────────────────────┤
│          BLOCK 1                    │
├─────────────────────────────────────┤
│           ...                       │
├─────────────────────────────────────┤
│          BLOCK N                    │
└─────────────────────────────────────┘
```

All multi-byte integers are **little-endian**.

---

## File Header (64 bytes)

| Offset | Size | Type | Name | Description |
|--------|------|------|------|-------------|
| 0 | 4 | `uint8[4]` | `magic` | `0x43 0x58 0x52 0x43` = "CXRC" |
| 4 | 2 | `uint16` | `version` | Format version. Current: `0x0001` |
| 6 | 1 | `uint8` | `stream_type` | Stream type code (see table below) |
| 7 | 1 | `uint8` | `codec_id` | Codec identifier (see table below) |
| 8 | 16 | `char[16]` | `symbol` | Null-padded ASCII, e.g. `"ETHUSDT\0\0\0\0\0\0\0\0\0"` |
| 24 | 1 | `uint8` | `exchange_id` | Exchange (matches `canon::ExchangeId`) |
| 25 | 1 | `uint8` | `market_type` | Market (matches `canon::MarketType`) |
| 26 | 6 | `uint8[6]` | `_reserved` | Must be zero |
| 32 | 8 | `int64` | `start_ts` | File start timestamp (nanoseconds since epoch) |
| 40 | 8 | `uint64` | `event_count_total` | Total events written (updated on close) |
| 48 | 8 | `uint64` | `block_count_total` | Total blocks written (updated on close) |
| 56 | 4 | `uint32` | `_reserved2` | Must be zero |
| 60 | 4 | `uint32` | `header_crc32c` | CRC-32C of bytes 0..59 of this header |

Total: 64 bytes.

> Note: `event_count_total` and `block_count_total` are written as zero on open and
> patched by `pwrite` on close. A file truncated mid-write will show zeros — the
> reader must handle this gracefully. The `header_crc32c` is computed **after**
> the close-time patch and written in the same `pwrite` transaction as the
> `event_count_total` / `block_count_total` update.

---

## Block Structure

Each block holds a batch of compressed events for one stream.

### Block Header (32 bytes)

| Offset | Size | Type | Name | Description |
|--------|------|------|------|-------------|
| 0 | 4 | `uint32` | `block_magic` | `0x42 0x4C 0x4B 0x00` = "BLK\0" |
| 4 | 4 | `uint32` | `block_type` | Block type (see table) |
| 8 | 4 | `uint32` | `compressed_size` | Bytes of compressed payload following this header |
| 12 | 4 | `uint32` | `event_count` | Number of events in this block |
| 16 | 8 | `int64` | `first_ts` | Timestamp of first event in block (ns) |
| 24 | 4 | `uint32` | `crc32` | CRC-32 of compressed payload bytes |
| 28 | 4 | `uint32` | `_reserved` | Must be zero |

Total: 32 bytes.

### Block Payload

`compressed_size` bytes of codec output immediately follow the block header.
The exact encoding depends on `codec_id` in the file header (all blocks in a file
use the same codec).

---

## Block Flush Policy

A recorder thread flushes the current block whenever **any** of the following
thresholds is reached (whichever fires first):

| Threshold | Value | Why this value |
|-----------|-------|----------------|
| `event_count` | `512` events | Matches the rANS quasi-adaptive rebuild boundary — the block boundary doubles as the model rebuild boundary, avoiding sub-block model drift. |
| `elapsed_wall_time` | `1` second | Caps write latency for slow streams (e.g. funding-style low-frequency data): at most 1 s between a live event and it being durable on disk. |
| `uncompressed_payload_bytes` | `256` KB | Caps per-block RAM (encoder working set fits in L2). Also caps the data lost on unclean shutdown. |

A block is additionally forced on:
- `SIGTERM` / `SIGINT` (graceful shutdown path)
- file rotation boundary (see "File Rotation" below)
- `GAP_MARKER` emission (see block_type table)

---

## CODER_RESET and Error Recovery

Every `1024` blocks, and on file rotation, the recorder emits a dedicated
`CODER_RESET` block (`block_type = 0x04`, `compressed_size = 0`, `event_count = 0`).
This block has no payload; the reader drains its adaptive model state on seeing it:

- Binary AC / range coder: all `count0[c]`, `count1[c]` → reset to the Laplace prior (0,0).
- rANS: `count0[c]`, `count1[c]` → prior; `rebuild_cdf()` runs with the prior distribution.
- Delta encoder: `last_price`, `last_ts`, `last_id`, level maps → cleared (the **next** data block is required to start with absolute values, same as block-start semantics in [DELTA_ENCODING.md](DELTA_ENCODING.md)).

This serves two purposes:
1. **Random-access replay** — a reader that wants to seek into the file forwards to the nearest `CODER_RESET` and starts decoding from there without needing the preceding block chain.
2. **Recovery after corruption** — on CRC mismatch, the reader skips data blocks until the next `CODER_RESET` and resumes, losing at most one reset window.

---

## Bitstream End and Block Payload Padding

The AC/rANS bitstream inside a data block is not byte-aligned during encode.
On block flush the encoder:
1. Flushes the final interval (emits `ceil(log2(1 / (R − L)))` bits derived from `L`).
2. Pads the last byte with zero bits up to a byte boundary.
3. Writes the resulting byte count into `compressed_size` in the block header.

The decoder uses `event_count` from the block header as the stop condition — it
decodes exactly that many events and ignores any trailing padding bits in the
final byte.

---

## stream_type Codes

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `AGG_TRADE` | `aggTrade` stream — `TradePublic` events |
| `0x02` | `DEPTH_UPDATE` | `depth@0ms` raw diff book updates (one event per exchange emit, ~10–20 ev/s/symbol) |
| `0x03` | `BOOK_TICKER` | `bookTicker` best bid/ask events |
| `0x04` | `DEPTH_SNAPSHOT` | Full order book REST snapshot (periodic, 60 s) |
| `0x05`–`0xFF` | _reserved_ | Reserved for future use (e.g. funding/mark — excluded from MVP) |

---

## block_type Codes

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `DATA` | Normal compressed event data |
| `0x02` | `SNAPSHOT` | Full order book snapshot (depth REST) |
| `0x03` | `GAP_MARKER` | Sequence ID gap detected; book state reset |
| `0x04` | `CODER_RESET` | Arithmetic coder model reset (adaptive models only) |
| `0xFF` | `FILE_END` | Sentinel block with zero payload; marks clean close |

A `GAP_MARKER` block carries no payload (`compressed_size = 0`, `event_count = 0`).
Its `first_ts` records the timestamp when the gap was detected.

A `CODER_RESET` block resets adaptive probability models to uniform priors.
Decoders must reset their model state when they encounter this block type.

---

## codec_id Codes

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `VARINT` | Delta + ZigZag VarInt, no arithmetic coding. Equivalent to FAST protocol. |
| `0x02` | `AC_BIN16_CTX0` | Binary range coder, 16-bit prob table, no context (global model). |
| `0x03` | `AC_BIN16_CTX8` | Binary range coder, 16-bit prob, 8-bit context (256 models). Best ratio. |
| `0x04` | `AC_BIN16_CTX12` | Binary range coder, 16-bit prob, 12-bit context (4096 models). |
| `0x05` | `AC_BIN32_CTX8` | Binary range coder, 32-bit prob, 8-bit context. Slower due to 128-bit mul. |
| `0x06` | `RANGE_CTX8` | Range coder without division, 8-bit context. ~120–150 MB/s decode. |
| `0x07` | `RANS_CTX8` | rANS order-1, 8-bit context. **~700 MB/s decode** — recommended for replay. |

See [CODEC_VARIANTS.md](CODEC_VARIANTS.md) for full descriptions, pseudocode, and benchmark targets.

---

## Hex Dump Example — File Header

```
Offset  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F
------  -------------------------------------------------------
0x0000  43 58 52 43 01 00 01 03  45 54 48 55 53 44 54 00   CXRC....ETHUSDT.
0x0010  00 00 00 00 00 00 00 00  02 01 00 00 00 00 00 00   ................
0x0020  00 E0 B6 5B 7D 9E 96 17  00 00 00 00 00 00 00 00   ...[}...........
0x0030  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
```

Decoding:
- `43 58 52 43` → magic "CXRC" ✓
- `01 00` → version 1
- `01` → stream_type `AGG_TRADE`
- `03` → codec_id `AC_BIN16_CTX8`
- `45 54 48 55 53 44 54 00 00 00 00 00 00 00 00 00` → "ETHUSDT" (null-padded)
- `02` → exchange_id Binance
- `01` → market_type futures (fapi)
- `00 E0 B6 5B 7D 9E 96 17` → start_ts 1713168000000000000 ns = 2024-04-15T12:00:00Z

---

## Reader Algorithm (Pseudocode)

```
open file
read FileHeader (64 bytes)
assert header.magic == "CXRC"
assert header.version == 1
init DeltaDecoder for header.stream_type
init ArithDecoder for header.codec_id

loop:
    read BlockHeader (32 bytes)
    if eof: break
    assert block.block_magic == "BLK\0"

    if block.block_type == GAP_MARKER:
        reset DeltaDecoder state
        continue

    if block.block_type == CODER_RESET:
        reset ArithDecoder model
        continue

    if block.block_type == FILE_END:
        break

    read block.compressed_size bytes → payload
    assert crc32(payload) == block.crc32

    decoded_events = ArithDecoder.decode(payload, block.event_count)
    for each delta_record in decoded_events:
        event = DeltaDecoder.reconstruct(delta_record)
        yield event
```

---

## File Rotation

Files are rotated when:
1. `rotation_interval` elapsed since `start_ts` (default: 3600 seconds)
2. `uncompressed_bytes_written` exceeds `max_uncompressed_size` (default: 512 MB)
3. Recorder receives SIGTERM/SIGINT — flushes current block, writes `FILE_END`, closes

On rotation:
1. Write `FILE_END` block
2. `pwrite` final `event_count_total` and `block_count_total` into file header
3. `fsync` and close
4. Open new file with fresh header

---

## References

- [OVERVIEW.md](OVERVIEW.md) — system context
- [STREAMS.md](STREAMS.md) — per-stream field specs
- [CODEC_VARIANTS.md](CODEC_VARIANTS.md) — codec_id descriptions
- [DELTA_ENCODING.md](DELTA_ENCODING.md) — what goes inside the compressed payload
