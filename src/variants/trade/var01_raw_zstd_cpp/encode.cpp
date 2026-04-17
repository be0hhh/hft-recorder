#include "variants/trade/var01_raw_zstd_cpp/encode.hpp"

// ─── Implementation guide for the next agent ─────────────────────────────────
//
// Goal: zstd-compress the block's raw event byte stream.
//
// Steps (see README.md in this directory for full context):
//   1. Validate args: in != nullptr, out != nullptr, out_cap > 0.
//   2. Call hftrec::support::zstdEncode(in, in_len, out, out_cap, level=3, out_written).
//      (ZstdWrapper is a Phase 1 stub that returns Unimplemented — land it first.)
//   3. If Status::OutOfRange, propagate — do NOT grow the buffer here; the
//      caller (BlockWriter) owns sizing decisions per doc/FILE_FORMAT.md.
//   4. On success set out_written to the compressed byte count.
//
// Hard rules (doc/CODING_STYLE.md):
//   - noexcept, Status-return, no exceptions, no heap alloc in this function.
//   - in and out buffers may not alias.
//
// Reference: doc/EXTERNAL_LIBRARIES.md §zstd, doc/CODEC_VARIANTS.md row ZSTD.
// ─────────────────────────────────────────────────────────────────────────────

namespace hftrec::variants::trade_var01 {

Status encodeBlock(const std::uint8_t*,
                   std::size_t,
                   std::uint8_t*,
                   std::size_t,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::variants::trade_var01
