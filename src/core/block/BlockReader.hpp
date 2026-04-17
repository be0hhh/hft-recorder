#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/block/BlockHeader.hpp"
#include "core/block/FileHeader.hpp"
#include "core/common/Status.hpp"

// BlockReader — open a .cxrec file, read FileHeader, iterate blocks, verify CRC.
// Phase 1: interface declared; implementation is Phase 2+.

namespace hftrec {

class BlockReader {
  public:
    BlockReader() = default;
    BlockReader(const BlockReader&)            = delete;
    BlockReader& operator=(const BlockReader&) = delete;
    ~BlockReader();

    Status open(std::string_view path) noexcept;
    Status fileHeader(FileHeader& out) noexcept;
    Status nextBlock(BlockHeader& outHdr, const std::uint8_t*& outPayload, std::size_t& outLen) noexcept;
    Status close() noexcept;

  private:
    int fd_{-1};
};

}  // namespace hftrec
