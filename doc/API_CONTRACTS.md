# hft-recorder — API Contracts (C++ interfaces)

Every reusable component in `apps/hft-recorder/src/core/` must implement one
of the interfaces defined here. Variants in `src/variants/` compose these
interfaces; the app layer (`src/app/`) wires them together.

All identifiers live under `namespace hftrec`. Nested namespaces follow the
source directory: `hftrec::codec`, `hftrec::block`, `hftrec::stream`, etc.

---

## 0. Common types

```cpp
// src/core/common/types.hpp
#pragma once

#include "cxet.hpp"                           // Price, Amount, TimeNs, Id, Symbol, Span
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"

#include <cstddef>
#include <cstdint>

namespace hftrec {

using cxet::Price;
using cxet::Amount;
using cxet::TimeNs;
using cxet::Id;
using cxet::CountVal;
using cxet::Symbol;
using cxet::TradePublic;
using cxet::BookTickerData;
using cxet::OrderBookSnapshot;

template<class T> using Span = cxet::Span<T>;

// Format enums — byte values must match FILE_FORMAT.md codec_id / stream_type tables.
enum class StreamType : std::uint8_t {
    AggTrade       = 0x01,
    DepthUpdate    = 0x02,   // depth@0ms raw diff
    BookTicker     = 0x03,
    DepthSnapshot  = 0x04,
};

enum class CodecId : std::uint8_t {
    Varint        = 0x01,
    AcBin16Ctx0   = 0x02,
    AcBin16Ctx8   = 0x03,
    AcBin16Ctx12  = 0x04,
    AcBin32Ctx8   = 0x05,
    RangeCtx8     = 0x06,
    RansCtx8      = 0x07,
};

enum class BlockType : std::uint32_t {
    Data          = 0x01,
    Snapshot      = 0x02,
    GapMarker     = 0x03,
    CoderReset    = 0x04,
    FileEnd       = 0xFF,
};

// Result codes returned by every fallible hot-path call.
// (No exceptions — see CODING_STYLE.md §1, §11.)
enum class Status : std::uint8_t {
    Ok                  = 0,
    NeedMoreInput       = 1,   // decoder: payload ended mid-event (should never happen in a well-formed file)
    OutputBufferFull    = 2,   // encoder: caller must flush before encoding more
    CrcMismatch         = 3,   // decoder: block payload failed CRC — skip to next CODER_RESET
    BadMagic            = 4,   // file/block header magic didn't match
    UnsupportedCodec    = 5,   // codec_id in file header is unknown to this binary
    IoError             = 6,   // pwrite/pread failed at syscall level
    InvalidArgument     = 7,
};

} // namespace hftrec
```

---

## 1. `IBlockEncoder` — the codec-side write half

Every concrete codec (VARINT, AC_BIN16_CTX0, …, RANS_CTX8) implements this
exactly. The encoder knows nothing about the file format — it only transforms
one block's worth of delta-encoded events into a byte payload.

```cpp
// src/core/codec/i_block_encoder.hpp
#pragma once

#include "core/common/types.hpp"

#include <cstddef>
#include <cstdint>

namespace hftrec::codec {

// Abstract base; concrete implementations are non-copyable and non-movable.
// The interface is deliberately minimal — no heap allocation, no exceptions,
// no ownership transfer.
class IBlockEncoder {
public:
    virtual ~IBlockEncoder() = default;

    // Must be called once at construction and on every CODER_RESET boundary.
    // After reset, encoder state is identical to a freshly constructed instance
    // for the same (streamType, codecId, ctxBits).
    virtual void reset() noexcept = 0;

    // Encode `events` (delta-record bytes produced by the delta layer) into
    // `out`. Returns `Status::Ok` on success.
    //
    // `in`:   contiguous bytes of the delta-encoded payload for ONE block.
    //         Caller is responsible for assembling this up-front.
    // `out`:  caller-owned output buffer. Must be at least `maxCompressedSize(in.size())`
    //         bytes. On `Ok`, `*outBytesWritten` holds the exact compressed
    //         byte count (this becomes `compressed_size` in the block header).
    //
    // Pre-condition: `in.size() <= kMaxBlockBytes`.
    // Post-condition on `Ok`: the returned payload + the encoder's internal
    // model state is sufficient for a matching IBlockDecoder to reproduce `in`
    // byte-for-byte.
    virtual Status encode(Span<const std::uint8_t> in,
                          Span<std::uint8_t>       out,
                          std::size_t*             outBytesWritten) noexcept = 0;

    // Upper bound on output size for a given input size. Pure function of
    // the codec: used by the caller to size `out` before calling encode().
    virtual std::size_t maxCompressedSize(std::size_t inBytes) const noexcept = 0;

    // Which codec this instance implements. Used by BlockWriter to fill
    // `codec_id` in the file header.
    virtual CodecId codecId() const noexcept = 0;
};

} // namespace hftrec::codec
```

### Concrete header shape

Every codec ships as a pair of files:

```
src/core/codec/varint_encoder.{hpp,cpp}
src/core/codec/varint_decoder.{hpp,cpp}
src/core/codec/ac_bin16_ctx0_encoder.{hpp,cpp}
src/core/codec/ac_bin16_ctx0_decoder.{hpp,cpp}
…
src/core/codec/rans_ctx8_encoder.{hpp,cpp}
src/core/codec/rans_ctx8_decoder.{hpp,cpp}
```

`src/core/codec/codec_factory.{hpp,cpp}` exposes:

```cpp
std::unique_ptr<IBlockEncoder> makeEncoder(CodecId id);
std::unique_ptr<IBlockDecoder> makeDecoder(CodecId id);
```

---

## 2. `IBlockDecoder` — the codec-side read half

```cpp
// src/core/codec/i_block_decoder.hpp
#pragma once

#include "core/common/types.hpp"

namespace hftrec::codec {

class IBlockDecoder {
public:
    virtual ~IBlockDecoder() = default;

    virtual void reset() noexcept = 0;

    // Inverse of IBlockEncoder::encode. `in` = one block's compressed payload
    // (length from BlockHeader::compressed_size). `out` must be at least
    // `expectedDecodedSize(in.size(), eventCount)` bytes.
    virtual Status decode(Span<const std::uint8_t> in,
                          std::uint32_t            eventCount,
                          Span<std::uint8_t>       out,
                          std::size_t*             outBytesWritten) noexcept = 0;

    // Upper bound on decoded payload size. Concrete codecs either have a
    // deterministic expansion ratio (varint) or query their own model.
    virtual std::size_t expectedDecodedSize(std::size_t   compressedBytes,
                                            std::uint32_t eventCount) const noexcept = 0;

    virtual CodecId codecId() const noexcept = 0;
};

} // namespace hftrec::codec
```

---

## 3. `IDeltaEncoder<Event>` — the field-level delta transform

The delta layer sits between the raw CXETCPP event and the byte-oriented
codec. It is specialized per `Event` type (`TradePublic`, `BookTickerData`,
`OrderBookSnapshot`). The contract is the same; only the `Event` changes.

```cpp
// src/core/stream/i_delta_encoder.hpp
#pragma once

#include "core/common/types.hpp"

namespace hftrec::stream {

// Serializes a sequence of `Event` into the delta-encoded byte stream
// consumed by IBlockEncoder. Stateful across events in the same block;
// must be reset at every block boundary and every CODER_RESET.
template<class Event>
class IDeltaEncoder {
public:
    virtual ~IDeltaEncoder() = default;

    virtual void reset() noexcept = 0;

    // Append one event to the delta stream.
    //
    // `ev`:  the next event (in the order the exchange produced them).
    // `out`: accumulating byte buffer owned by the writer thread;
    //        sized to at least kMaxBlockBytes + overhead upfront.
    //
    // Returns `Status::OutputBufferFull` when appending would exceed `out.size()`,
    // signalling the writer that it must flush the current block first.
    virtual Status appendEvent(const Event&       ev,
                               Span<std::uint8_t> out,
                               std::size_t*       outBytesWritten) noexcept = 0;

    // True when the next appended event will start a new block (first event
    // in the block uses absolute values — see DELTA_ENCODING.md).
    virtual bool isBlockStart() const noexcept = 0;

    virtual StreamType streamType() const noexcept = 0;
};

template<class Event>
class IDeltaDecoder {
public:
    virtual ~IDeltaDecoder() = default;
    virtual void reset() noexcept = 0;

    // Inverse: read `eventCount` events out of `in` into `out`.
    virtual Status decodeEvents(Span<const std::uint8_t> in,
                                std::uint32_t            eventCount,
                                Span<Event>              out) noexcept = 0;

    virtual StreamType streamType() const noexcept = 0;
};

} // namespace hftrec::stream
```

Concrete delta encoders live next to their event type:

```
src/core/stream/trade_delta_encoder.{hpp,cpp}   // implements IDeltaEncoder<TradePublic>
src/core/stream/trade_delta_decoder.{hpp,cpp}
src/core/stream/book_ticker_delta_encoder.{hpp,cpp}
src/core/stream/book_ticker_delta_decoder.{hpp,cpp}
src/core/stream/orderbook_delta_encoder.{hpp,cpp}
src/core/stream/orderbook_delta_decoder.{hpp,cpp}
src/core/stream/snapshot_delta_encoder.{hpp,cpp}
src/core/stream/snapshot_delta_decoder.{hpp,cpp}
```

---

## 4. `BlockWriter` — concrete file-format layer

`BlockWriter` is **not** an interface. It's one concrete type in
`src/core/block/block_writer.{hpp,cpp}` that both the recorder and the bench
tool use. It owns one open file descriptor, one delta encoder, and one block
encoder.

```cpp
// src/core/block/block_writer.hpp
#pragma once

#include "core/common/types.hpp"
#include "core/codec/i_block_encoder.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace hftrec::block {

struct FileHeader;    // #pragma pack(1) struct matching FILE_FORMAT.md byte-for-byte
struct BlockHeader;   // ditto

struct WriterConfig {
    std::string          path;                        // absolute path to the .cxrec file
    StreamType           streamType;
    CodecId              codecId;
    Symbol               symbol;
    std::uint8_t         exchangeId;
    std::uint8_t         marketType;
    TimeNs               startTs;

    // Block flush policy — see FILE_FORMAT.md "Block Flush Policy"
    std::uint32_t        maxEventsPerBlock     = 512;
    std::chrono::milliseconds maxBlockWallTime{1000};
    std::uint32_t        maxBlockBytes         = 256 * 1024;
    std::uint32_t        coderResetEveryBlocks = 1024;
};

class BlockWriter {
public:
    explicit BlockWriter(WriterConfig                         cfg,
                         std::unique_ptr<codec::IBlockEncoder> encoder);
    ~BlockWriter();

    BlockWriter(const BlockWriter&)            = delete;
    BlockWriter& operator=(const BlockWriter&) = delete;

    // Appends one delta-encoded byte range produced by IDeltaEncoder::appendEvent.
    // Handles block flush policy internally: calls `flushBlock()` when any
    // threshold is reached. Also emits CODER_RESET every coderResetEveryBlocks.
    Status appendEventBytes(Span<const std::uint8_t> bytes, TimeNs eventTs) noexcept;

    // Force a block boundary now (e.g. on SIGTERM). Caller must hold sole
    // producer responsibility for this writer.
    Status flushBlock() noexcept;

    // Emit a GAP_MARKER block (compressed_size = 0). Caller must also reset
    // its delta encoder state — BlockWriter does not own the delta layer.
    Status emitGapMarker(TimeNs gapTs) noexcept;

    // Emit a FILE_END sentinel and patch the file header counts. Idempotent.
    Status close() noexcept;

    // Metrics surface: see LOGGING_AND_METRICS.md for the Prometheus mapping.
    std::uint64_t eventCountTotal() const noexcept;
    std::uint64_t blockCountTotal() const noexcept;
    std::uint64_t bytesWrittenTotal() const noexcept;
};

} // namespace hftrec::block
```

`BlockReader` is its inverse, in the same directory.

---

## 5. `IStreamRecorder` — the per-stream capture glue

One concrete `StreamRecorder` template implements this; there is no
alternative implementation expected. It lives in
`src/core/online/stream_recorder.hpp`.

```cpp
// src/core/online/i_stream_recorder.hpp
#pragma once

#include "core/common/types.hpp"

#include <atomic>
#include <thread>

namespace hftrec::online {

// Owns: one CXETCPP subscription (CxetStream<Event> OR a runSubscribe… callback),
// one IDeltaEncoder<Event>, one BlockWriter, one SPSC ring between producer
// and writer thread, and the two threads that service them.
//
// Exposed as an abstract base so the app layer can hold a vector<unique_ptr<IStreamRecorder>>
// even though the concrete types (TradeRecorder, BookTickerRecorder, …) are
// different template instantiations.
class IStreamRecorder {
public:
    virtual ~IStreamRecorder() = default;

    // Starts producer + writer threads. Non-blocking. Returns immediately.
    virtual Status start() noexcept = 0;

    // Signals threads to stop, waits for join, flushes the final block,
    // writes FILE_END. Safe to call multiple times (idempotent).
    virtual Status stop() noexcept = 0;

    // Poll metrics — called by the control thread every N seconds to push
    // gauges to Prometheus. Read-only: safe from any thread.
    virtual std::uint64_t eventsCaptured() const noexcept = 0;
    virtual std::uint64_t eventsDropped()  const noexcept = 0;   // SPSC overflow
    virtual std::uint64_t bytesOnDisk()    const noexcept = 0;

    virtual StreamType streamType() const noexcept = 0;
};

} // namespace hftrec::online
```

Concrete instantiations:

```
src/core/online/trade_recorder.hpp          // IStreamRecorder via CxetStream<TradePublic>
src/core/online/book_ticker_recorder.hpp    // via CxetStream<BookTickerData>
src/core/online/orderbook_delta_recorder.hpp // via runSubscribeOrderBookDeltaByConfig
src/core/online/snapshot_poller.hpp         // via runGetOrderBookByConfig + timer
```

---

## 6. `IVariant` — the comparison harness entry point

Each directory under `src/variants/<family>/varNN_…_cpp/` implements this in
its `encode.hpp` / `decode.hpp`. The bench harness (`bench/main.cpp`)
discovers variants via `registerVariant()` at static-init time — no reflection,
no build-system generators.

```cpp
// src/core/common/i_variant.hpp
#pragma once

#include "core/common/types.hpp"

#include <string>

namespace hftrec {

struct VariantMetadata {
    std::string  id;              // "trade.var02.delta_varint_cpp"
    std::string  family;          // "trade" | "l1" | "orderbook"
    std::string  profile;         // "live" | "archive" | "replay"
    std::string  description;
};

// Variants operate on a whole captured dataset at once (bench-driven).
// They do NOT need to match IBlockEncoder — a variant can choose any
// representation or pipeline.
class IVariant {
public:
    virtual ~IVariant() = default;

    virtual const VariantMetadata& metadata() const noexcept = 0;

    // Encode `inDataset` (a whole .cxrec file worth of raw events, already
    // decoded) into `outCompressed`. Returns total compressed bytes written.
    //
    // This is the **offline** path — allocation is OK here.
    virtual Status encode(Span<const std::uint8_t> inDataset,
                          std::string*             outCompressedPath) = 0;

    virtual Status decode(const std::string& inCompressedPath,
                          Span<std::uint8_t> outDataset,
                          std::size_t*       outBytesWritten) = 0;
};

void                registerVariant(std::unique_ptr<IVariant>);
Span<const IVariant*> allVariants() noexcept;

} // namespace hftrec
```

The bench harness then loops `for (auto* v : allVariants()) { … }` against
every captured dataset.

---

## 7. `SpscRing<T, Capacity>` — producer→writer queue

Not an interface; one concrete header-only type in
`src/core/common/spsc_ring.hpp`. Every `IStreamRecorder` owns one instance.

```cpp
// src/core/common/spsc_ring.hpp
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

namespace hftrec {

// Wait-free SPSC queue, power-of-two capacity. T must be trivially copyable
// (TradePublic, BookTickerData, OrderBookSnapshot all qualify).
template<class T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    // Called by producer thread only. Returns false when the ring is full.
    [[nodiscard]] bool tryPush(const T& value) noexcept;

    // Called by consumer thread only. Returns false when the ring is empty.
    [[nodiscard]] bool tryPop(T& out) noexcept;

    std::size_t approxSize() const noexcept;

private:
    alignas(64) std::atomic<std::size_t> head_{0};        // producer cursor
    alignas(64) std::atomic<std::size_t> tail_{0};        // consumer cursor
    alignas(64) std::array<T, Capacity>  slots_{};
};

} // namespace hftrec
```

---

## 8. Ownership summary (who owns what)

```
main()                            [owns]  std::vector<std::unique_ptr<IStreamRecorder>>
  └── TradeRecorder               [owns]  CxetStream<TradePublic>
                                          SpscRing<TradePublic, 2048>
                                          std::thread producer
                                          std::thread writer
                                          BlockWriter
                                            └── IBlockEncoder     (unique_ptr)
                                            └── IDeltaEncoder<…>  (unique_ptr)
  └── BookTickerRecorder          [owns]  (same shape, BookTickerData)
  └── OrderBookDeltaRecorder      [owns]  std::thread that calls runSubscribe…ByConfig
                                          SpscRing<OrderBookSnapshot, 512>
                                          BlockWriter + codecs
  └── SnapshotPoller              [owns]  std::thread sleep-poll loop
                                          SpscRing<OrderBookSnapshot, 16>
                                          BlockWriter + codecs
```

No shared ownership anywhere. The only atomic data crossing threads is the
SPSC ring state. Configuration is passed by value at construction; shutdown
state is a single `std::atomic<bool>` per recorder.

---

## 9. Header file checklist

For every new interface:

- [ ] Lives under `src/core/<subsystem>/` or `src/variants/<family>/<variant>/`.
- [ ] Header name matches the primary type: `block_encoder.hpp`.
- [ ] Contains exactly one primary type; helpers can be in same header or a sibling.
- [ ] Uses `Span<T>`, never `T* + size_t`.
- [ ] All public methods are `noexcept`.
- [ ] All public methods return `Status` or primitive POD.
- [ ] `= default` destructor when possible; `virtual` only on true polymorphic bases.
- [ ] No `using namespace` in the header.

---

## References

- [FILE_FORMAT.md](FILE_FORMAT.md) — byte layout for `BlockWriter` / `BlockReader`
- [DELTA_ENCODING.md](DELTA_ENCODING.md) — field-level rules for every `IDeltaEncoder<Event>`
- [ARITHMETIC_CODING.md](ARITHMETIC_CODING.md) — per-codec internals behind `IBlockEncoder`
- [CXETCPP_USAGE_EXAMPLES.md](CXETCPP_USAGE_EXAMPLES.md) — how `IStreamRecorder` drives CXETCPP
- [CODING_STYLE.md](CODING_STYLE.md) — naming, `noexcept`, `alignas`, etc.
