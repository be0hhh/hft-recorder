#include "FileHeader.hpp"
#include "../Codec/Crc32c.hpp"
#include <cstddef>

#include <cstring>

namespace hftrec {

Status fileHeaderSerialize(const FileHeader& h, std::uint8_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap < sizeof(FileHeader)) return Status::InvalidArgument;
    auto header = h;
    header.crc32c = codec::crc32c(reinterpret_cast<const std::uint8_t*>(&header),
                                 offsetof(FileHeader, crc32c));
    std::memcpy(out, &header, sizeof(header));
    return Status::Ok;
}

Status fileHeaderParse(const std::uint8_t* in, std::size_t len, FileHeader& out) noexcept {
    if (in == nullptr || len < sizeof(FileHeader)) return Status::InvalidArgument;
    std::memcpy(&out, in, sizeof(FileHeader));
    if (out.magic != constants::kFileMagic || out.flags != 0u ||
        out.stream < 1u || out.stream > 3u || out.reserved1 != 0u ||
        out.crc32c != codec::crc32c(in, offsetof(FileHeader, crc32c))) return Status::CorruptData;
    for (const auto byte : out.reserved0) if (byte != 0u) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec
