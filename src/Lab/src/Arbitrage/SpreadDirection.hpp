#pragma once

#include <cstdint>

namespace hftrec::arbitrage {

enum class SpreadDirection : std::uint8_t {
    None = 0,
    BuyAAskSellBBid = 1,
    BuyBAskSellABid = 2,
};

}  // namespace hftrec::arbitrage
