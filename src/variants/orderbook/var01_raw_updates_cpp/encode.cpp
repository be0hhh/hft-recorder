#include "variants/orderbook/var01_raw_updates_cpp/encode.hpp"

// ─── Implementation guide ───────────────────────────────────────────────────
//
// Goal: null-transform serializer for OrderBookDeltaEvent arrays.
//
// Steps (see README.md here):
//   1. Walk input events. Each event's serialized size = sizeof(UpdateHeader)
//      + price_count * sizeof(LevelEntry).
//   2. Accumulate bytes into `out`, bailing with Status::OutOfRange if out_cap
//      is ever exceeded — the caller chose the buffer size.
//   3. On success set out_written = total bytes emitted.
//
// Reference: doc/DELTA_ENCODING.md §OrderBook, doc/FILE_FORMAT.md §Block layout.
// ────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::orderbook_var01 {

Status encodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::orderbook_var01
