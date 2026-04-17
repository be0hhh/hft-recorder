#pragma once

#include <cstddef>
#include <cstdint>

// CRC32C (Castagnoli polynomial 0x1EDC6F41, reflected).
// Reference vector: crc32c("123456789") == 0xE3069283.

namespace hftrec::codec {

std::uint32_t crc32c(const std::uint8_t* data, std::size_t len) noexcept;

std::uint32_t crc32cUpdate(std::uint32_t seed, const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace hftrec::codec
