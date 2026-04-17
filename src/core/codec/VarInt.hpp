#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

// Unsigned LEB128 — 1..10 bytes for 64-bit values.

namespace hftrec::codec {

Status varintEncode(std::uint64_t value, std::uint8_t* out, std::size_t cap, std::size_t& written) noexcept;

Status varintDecode(const std::uint8_t* in, std::size_t len, std::size_t& consumed, std::uint64_t& out) noexcept;

}  // namespace hftrec::codec
