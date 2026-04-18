#include "variants/orderbook/var01_raw_updates_cpp/encode.hpp"

#include <cstring>

// Identity codec: copies `in` to `out` unchanged. Acts as the baseline reference
// variant for the compression lab — its compression ratio is always 1.0, its
// roundtrip is trivially bit-exact, and its encode/decode throughput bounds
// what any real codec must beat to be worth the CPU cost.

namespace hftrec::variants::orderbook_var01 {

Status encodeBlock(const std::uint8_t* in,
                   std::size_t in_len,
                   std::uint8_t* out,
                   std::size_t out_cap,
                   std::size_t& out_written) noexcept {
    out_written = 0;
    if (in == nullptr && in_len != 0) return Status::InvalidArgument;
    if (out == nullptr && in_len != 0) return Status::InvalidArgument;
    if (in_len > out_cap) return Status::OutOfRange;
    if (in_len != 0) std::memcpy(out, in, in_len);
    out_written = in_len;
    return Status::Ok;
}

}  // namespace hftrec::variants::orderbook_var01
