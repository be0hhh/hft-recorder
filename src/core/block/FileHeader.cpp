#include "core/block/FileHeader.hpp"

#include <cstring>

namespace hftrec {

Status fileHeaderSerialize(const FileHeader& h, std::uint8_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap < sizeof(FileHeader)) return Status::InvalidArgument;
    std::memcpy(out, &h, sizeof(FileHeader));
    return Status::Ok;
}

Status fileHeaderParse(const std::uint8_t* in, std::size_t len, FileHeader& out) noexcept {
    if (in == nullptr || len < sizeof(FileHeader)) return Status::InvalidArgument;
    std::memcpy(&out, in, sizeof(FileHeader));
    if (out.magic != constants::kFileMagic) return Status::CorruptData;
    if (out.version != constants::kFileVersion) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec
