#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/common/Status.hpp"
#include "core/dataset/StreamFamily.hpp"

// BlockWriter — append events into the current block buffer; flush when any of
// the three thresholds (event_count / wall_time / bytes) is reached. Flush writes
// a BlockHeader + payload via pwrite, bumps the file offset, and runs fsync
// every kFsyncEveryBlocks.
//
// Phase 1: interface declared; implementation is Phase 2.

namespace hftrec {

struct WriterConfig {
    std::string_view outputPath{};
    StreamFamily     family{StreamFamily::TradeLike};
    std::uint8_t     codec{0x01};  // VARINT by default
    std::size_t      maxEvents{0};
    std::uint32_t    maxWallMs{0};
    std::size_t      maxBytes{0};
};

class BlockWriter {
  public:
    BlockWriter() = default;
    BlockWriter(const BlockWriter&)            = delete;
    BlockWriter& operator=(const BlockWriter&) = delete;
    ~BlockWriter();

    Status open(const WriterConfig& cfg) noexcept;
    Status appendEvent(const std::uint8_t* payload, std::size_t len) noexcept;
    Status flush() noexcept;
    Status close() noexcept;

  private:
    int          fd_{-1};
    std::size_t  offset_{0};
    WriterConfig cfg_{};
};

}  // namespace hftrec
