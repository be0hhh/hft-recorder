#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

// Trade delta encoder / decoder — declarations only (Phase 1 scaffold).
// Implementations land in variants (e.g. src/variants/trade/var02_delta_varint_cpp/)
// or in src/core/representation once promoted.

namespace hftrec {

struct TradeEvent;  // defined in variants; placeholder until CXETCPP alias lands here

class ITradeDeltaEncoder {
  public:
    virtual ~ITradeDeltaEncoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status appendEvent(const TradeEvent& ev, std::uint8_t* out, std::size_t cap, std::size_t& written) noexcept = 0;
};

class ITradeDeltaDecoder {
  public:
    virtual ~ITradeDeltaDecoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status decodeEvent(const std::uint8_t* in, std::size_t len, std::size_t& consumed, TradeEvent& out) noexcept = 0;
};

}  // namespace hftrec
