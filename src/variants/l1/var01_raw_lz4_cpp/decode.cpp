#include "variants/l1/var01_raw_lz4_cpp/decode.hpp"

// ─── Implementation guide ───────────────────────────────────────────────────
//
// Goal: inverse of encode.cpp.
//
// Steps:
//   1. Validate args.
//   2. out_cap must equal BlockHeader.event_count * sizeof(BookTickerRow).
//   3. Call hftrec::support::lz4Decode(in, in_len, out, out_cap, out_written).
//      If out_written != expected size → Status::CorruptData.
//
// Reference: doc/FILE_FORMAT.md §Block layout.
// ────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::l1_var01 {

Status decodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::l1_var01
