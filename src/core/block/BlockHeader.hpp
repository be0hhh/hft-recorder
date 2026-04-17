#pragma once

#include <cstdint>

#include "core/common/Status.hpp"
#include "core/common/constants.hpp"

// BlockHeader — 32 bytes. Layout per doc/FILE_FORMAT.md.

namespace hftrec {

#pragma pack(push, 1)
struct BlockHeader {
    std::uint32_t magic;         // 'BLK\0' = 0x004B4C42
    std::uint8_t  stream;        // StreamFamily id
    std::uint8_t  codec;         // codec id
    std::uint8_t  flags;         // bit 0 = CODER_RESET, bit 1 = HAS_GAP, ...
    std::uint8_t  reserved0;
    std::uint32_t event_count;
    std::uint32_t payload_size;  // bytes following this header
    std::uint64_t first_ts_ns;   // first event ns
    std::uint32_t reserved1;
    std::uint32_t crc32c;        // CRC32C of payload bytes
};
#pragma pack(pop)

static_assert(sizeof(BlockHeader) == constants::kBlockHeaderBytes,
              "BlockHeader must be exactly 32 bytes");

Status blockHeaderSerialize(const BlockHeader& h, std::uint8_t* out, std::size_t cap) noexcept;
Status blockHeaderParse(const std::uint8_t* in, std::size_t len, BlockHeader& out) noexcept;

}  // namespace hftrec
