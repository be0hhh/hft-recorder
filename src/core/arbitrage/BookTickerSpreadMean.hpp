#pragma once

#include <cstdint>
#include <vector>

#include "core/arbitrage/BookTickerSpread.hpp"

namespace hftrec::arbitrage {

struct BookTickerSpreadMeanPoint {
    std::int64_t tsNs{0};
    double meanBps{0.0};
    double deviationBps{0.0};
    double costBandBps{0.0};
    double edgeAfterCostBps{0.0};
};

std::vector<BookTickerSpreadMeanPoint> buildRollingBookTickerSpreadMean(
    const std::vector<BookTickerSpreadPoint>& points,
    std::int64_t windowNs,
    double feePenaltyBps = 0.0);

}  // namespace hftrec::arbitrage
