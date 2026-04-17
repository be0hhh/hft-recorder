#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

namespace hftrec {

struct OrderBookDeltaEvent;

class IOrderBookDeltaEncoder {
  public:
    virtual ~IOrderBookDeltaEncoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status appendEvent(const OrderBookDeltaEvent& ev, std::uint8_t* out, std::size_t cap, std::size_t& written) noexcept = 0;
};

class IOrderBookDeltaDecoder {
  public:
    virtual ~IOrderBookDeltaDecoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status decodeEvent(const std::uint8_t* in, std::size_t len, std::size_t& consumed, OrderBookDeltaEvent& out) noexcept = 0;
};

}  // namespace hftrec
