#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

namespace hftrec::support {

Status lz4Encode(const std::uint8_t* in, std::size_t in_len,
                 std::uint8_t* out, std::size_t out_cap,
                 int accel, std::size_t& out_written) noexcept;

Status lz4Decode(const std::uint8_t* in, std::size_t in_len,
                 std::uint8_t* out, std::size_t out_cap,
                 std::size_t& out_written) noexcept;

}  // namespace hftrec::support
