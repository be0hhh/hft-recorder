#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

namespace hftrec::variants::orderbook_var01 {

Status decodeBlock(const std::uint8_t* in, std::size_t in_len,
                   std::uint8_t* out, std::size_t out_cap,
                   std::size_t& out_written) noexcept;

}  // namespace hftrec::variants::orderbook_var01
