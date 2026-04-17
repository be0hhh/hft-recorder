#include "core/block/BlockReader.hpp"

namespace hftrec {

BlockReader::~BlockReader() = default;

Status BlockReader::open(std::string_view path) noexcept {
    (void)path;
    return Status::Unimplemented;
}

Status BlockReader::fileHeader(FileHeader& out) noexcept {
    (void)out;
    return Status::Unimplemented;
}

Status BlockReader::nextBlock(BlockHeader& outHdr,
                              const std::uint8_t*& outPayload,
                              std::size_t& outLen) noexcept {
    (void)outHdr;
    (void)outPayload;
    (void)outLen;
    return Status::Unimplemented;
}

Status BlockReader::close() noexcept {
    return Status::Unimplemented;
}

}  // namespace hftrec
