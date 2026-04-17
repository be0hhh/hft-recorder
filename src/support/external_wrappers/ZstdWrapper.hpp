#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

// Thin zstd wrapper — linked in Phase 3. Phase 1 stub returns Unimplemented.

namespace hftrec::support {

Status zstdEncode(const std::uint8_t* in, std::size_t in_len,
                  std::uint8_t* out, std::size_t out_cap,
                  int level, std::size_t& out_written) noexcept;

Status zstdDecode(const std::uint8_t* in, std::size_t in_len,
                  std::uint8_t* out, std::size_t out_cap,
                  std::size_t& out_written) noexcept;

}  // namespace hftrec::support
