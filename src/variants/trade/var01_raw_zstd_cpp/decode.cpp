#include "variants/trade/var01_raw_zstd_cpp/decode.hpp"

// ─── Implementation guide for the next agent ─────────────────────────────────
//
// Goal: undo the zstd transform to recover the raw event byte stream.
//
// Steps:
//   1. Validate args: in != nullptr, out != nullptr, out_cap >= decompressed size.
//   2. Call hftrec::support::zstdDecode(in, in_len, out, out_cap, out_written).
//   3. The caller knows the expected uncompressed size from
//      BlockHeader.event_count * sizeof(TradeRow). If the decoder produces
//      a mismatch, return Status::CorruptData.
//
// Reference: doc/FILE_FORMAT.md §Block layout, doc/EXTERNAL_LIBRARIES.md §zstd.
// ─────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::trade_var01 {

Status decodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::trade_var01
