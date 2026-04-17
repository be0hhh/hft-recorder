#pragma once

#include <cstdint>

#include "core/common/Status.hpp"
#include "core/common/constants.hpp"

// FileHeader — 64 bytes. Layout per doc/FILE_FORMAT.md.

namespace hftrec {

#pragma pack(push, 1)
struct FileHeader {
    std::uint32_t magic;         // 'CXRC' = 0x43585243
    std::uint16_t version;       // 1
    std::uint16_t flags;         // reserved
    std::uint8_t  stream;        // StreamFamily id
    std::uint8_t  codec;         // codec id at record time (0x01 = VARINT)
    std::uint8_t  reserved0[6];  // pad to 16
    std::uint64_t created_ns;    // wall-clock creation timestamp (ns since epoch)
    char          symbol[16];    // right-padded with '\0'
    char          exchange[16];  // right-padded with '\0'
    std::uint32_t reserved1;     // pad to 60
    std::uint32_t crc32c;        // CRC32C of bytes 0..59
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == constants::kFileHeaderBytes,
              "FileHeader must be exactly 64 bytes");

Status fileHeaderSerialize(const FileHeader& h, std::uint8_t* out, std::size_t cap) noexcept;
Status fileHeaderParse(const std::uint8_t* in, std::size_t len, FileHeader& out) noexcept;

}  // namespace hftrec
