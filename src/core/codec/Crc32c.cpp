#include "core/codec/Crc32c.hpp"

#include <array>

namespace hftrec::codec {

namespace {

constexpr std::array<std::uint32_t, 256> makeTable() {
    constexpr std::uint32_t kPoly = 0x82F63B78u;  // reflected 0x1EDC6F41
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? ((c >> 1) ^ kPoly) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}

constexpr auto kTable = makeTable();

}  // namespace

std::uint32_t crc32cUpdate(std::uint32_t seed, const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t c = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = kTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t crc32c(const std::uint8_t* data, std::size_t len) noexcept {
    return crc32cUpdate(0, data, len);
}

}  // namespace hftrec::codec
