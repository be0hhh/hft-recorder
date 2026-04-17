#pragma once

#include <cstddef>
#include <cstdint>

#include "core/common/Status.hpp"

namespace hftrec {

struct BookTickerEvent;

class IBookTickerDeltaEncoder {
  public:
    virtual ~IBookTickerDeltaEncoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status appendEvent(const BookTickerEvent& ev, std::uint8_t* out, std::size_t cap, std::size_t& written) noexcept = 0;
};

class IBookTickerDeltaDecoder {
  public:
    virtual ~IBookTickerDeltaDecoder() = default;
    virtual Status reset() noexcept = 0;
    virtual Status decodeEvent(const std::uint8_t* in, std::size_t len, std::size_t& consumed, BookTickerEvent& out) noexcept = 0;
};

}  // namespace hftrec
