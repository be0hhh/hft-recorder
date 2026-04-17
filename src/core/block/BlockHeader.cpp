#include "core/block/BlockHeader.hpp"

#include <cstring>

namespace hftrec {

Status blockHeaderSerialize(const BlockHeader& h, std::uint8_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap < sizeof(BlockHeader)) return Status::InvalidArgument;
    std::memcpy(out, &h, sizeof(BlockHeader));
    return Status::Ok;
}

Status blockHeaderParse(const std::uint8_t* in, std::size_t len, BlockHeader& out) noexcept {
    if (in == nullptr || len < sizeof(BlockHeader)) return Status::InvalidArgument;
    std::memcpy(&out, in, sizeof(BlockHeader));
    if (out.magic != constants::kBlockMagic) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec
