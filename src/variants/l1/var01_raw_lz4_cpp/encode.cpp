#include "variants/l1/var01_raw_lz4_cpp/encode.hpp"

// ─── Implementation guide ───────────────────────────────────────────────────
//
// Goal: lz4-compress the block's raw event byte stream (BookTickerRow array).
//
// Steps (see README.md here):
//   1. Validate in/out pointers and lengths.
//   2. Call hftrec::support::lz4Encode(in, in_len, out, out_cap, accel=1, out_written).
//   3. If out_cap < LZ4_compressBound(in_len), return Status::OutOfRange; caller resizes.
//
// Reference: doc/EXTERNAL_LIBRARIES.md §lz4, doc/CODEC_VARIANTS.md row LZ4.
// ────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::l1_var01 {

Status encodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::l1_var01
