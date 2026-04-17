#pragma once

#include <cstdint>
#include <string_view>

// Three first-class stream families per doc/OVERVIEW.md.

namespace hftrec {

enum class StreamFamily : std::uint8_t {
    TradeLike        = 1,  // aggTrade / raw trade
    L1BookTicker     = 2,  // best bid / best ask
    OrderbookUpdates = 3,  // diff book / snapshot hybrid
};

inline constexpr std::string_view streamFamilyToString(StreamFamily f) noexcept {
    switch (f) {
        case StreamFamily::TradeLike:        return "trade_like";
        case StreamFamily::L1BookTicker:     return "l1_bookticker";
        case StreamFamily::OrderbookUpdates: return "orderbook_updates";
    }
    return "unknown";
}

}  // namespace hftrec
