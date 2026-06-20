#pragma once

#include <cstdint>
#include <vector>

#include "core/arbitrage/SpreadDirection.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::arbitrage {

struct BookTickerSpreadPoint {
    std::int64_t tsNs{0};
    double rawSpreadBps{0.0};
    double internalPenaltyBps{0.0};
    double feePenaltyBps{0.0};
    double spreadBps{0.0};
    SpreadDirection direction{SpreadDirection::None};
    std::int64_t buyAskPriceE8{0};
    std::int64_t sellBidPriceE8{0};
};

std::vector<BookTickerSpreadPoint> buildBestSideBookTickerSpread(
    const std::vector<hftrec::replay::BookTickerRow>& a,
    const std::vector<hftrec::replay::BookTickerRow>& b,
    double feePenaltyBps = 0.0);

}  // namespace hftrec::arbitrage
