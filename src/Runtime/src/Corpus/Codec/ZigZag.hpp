#pragma once

#include <cstdint>

namespace hftrec::codec {

inline constexpr std::uint64_t zigzagEncode(std::int64_t v) noexcept {
    return static_cast<std::uint64_t>((v << 1) ^ (v >> 63));
}

inline constexpr std::int64_t zigzagDecode(std::uint64_t u) noexcept {
    return static_cast<std::int64_t>((u >> 1) ^ -static_cast<std::int64_t>(u & 1));
}

}  // namespace hftrec::codec
