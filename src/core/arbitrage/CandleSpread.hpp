#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/arbitrage/SpreadDirection.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::arbitrage {

struct CandleSpreadSource {
    std::vector<hftrec::replay::CandleRow> rows{};
    std::string marketHint{};
};

struct CandleSpreadPoint {
    std::int64_t tsNs{0};
    double spreadBps{0.0};
    SpreadDirection direction{SpreadDirection::None};
    std::int64_t aCloseE8{0};
    std::int64_t bCloseE8{0};
    std::int64_t durationNs{0};
};

std::vector<hftrec::replay::CandleRow> selectCompareCandles(
    const std::vector<hftrec::replay::CandleRow>& rows);

std::vector<CandleSpreadPoint> buildBestSideCandleSpread(
    const CandleSpreadSource& a,
    const CandleSpreadSource& b);

}  // namespace hftrec::arbitrage
