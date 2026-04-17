#include "variants/orderbook/var01_raw_updates_cpp/decode.hpp"

// ─── Implementation guide ───────────────────────────────────────────────────
//
// Goal: parse the raw OrderBookDeltaEvent byte stream back into events.
//
// Steps:
//   1. Read UpdateHeader at the current offset, then price_count LevelEntry pairs.
//   2. Advance offset; repeat until in_len bytes consumed.
//   3. out_written = bytes emitted into the caller's event buffer. If in_len
//      runs out mid-event → Status::CorruptData.
//
// Reference: doc/DELTA_ENCODING.md §OrderBook.
// ────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::orderbook_var01 {

Status decodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::orderbook_var01
