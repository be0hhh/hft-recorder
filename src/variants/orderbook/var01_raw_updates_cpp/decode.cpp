#include "variants/orderbook/var01_raw_updates_cpp/decode.hpp"

#include <cstring>

// Identity codec decode — mirrors encode: copies `in` to `out` unchanged.

namespace hftrec::variants::orderbook_var01 {

Status decodeBlock(const std::uint8_t* in,
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
