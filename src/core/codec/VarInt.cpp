#include "core/codec/VarInt.hpp"

namespace hftrec::codec {

Status varintEncode(std::uint64_t value, std::uint8_t* out, std::size_t cap, std::size_t& written) noexcept {
    written = 0;
    if (out == nullptr) return Status::InvalidArgument;
    while (true) {
        if (written >= cap) return Status::OutOfRange;
        auto byte = static_cast<std::uint8_t>(value & 0x7Fu);
        value >>= 7;
        if (value != 0) byte |= 0x80u;
        out[written++] = byte;
        if (value == 0) break;
    }
    return Status::Ok;
}

Status varintDecode(const std::uint8_t* in, std::size_t len, std::size_t& consumed, std::uint64_t& out) noexcept {
    consumed = 0;
    out = 0;
    if (in == nullptr) return Status::InvalidArgument;
    std::uint64_t acc = 0;
    unsigned shift = 0;
    while (consumed < len) {
        const auto byte = in[consumed++];
        acc |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            out = acc;
            return Status::Ok;
        }
        shift += 7;
        if (shift >= 70) return Status::InvalidArgument;  // too long
    }
    return Status::OutOfRange;  // ran out of input before terminator
}

}  // namespace hftrec::codec
