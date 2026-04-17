#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

namespace hftrec::variants::l1_var01 {

Status encodeBlock(const std::uint8_t* in, std::size_t in_len,
                   std::uint8_t* out, std::size_t out_cap,
                   std::size_t& out_written) noexcept;

}  // namespace hftrec::variants::l1_var01
