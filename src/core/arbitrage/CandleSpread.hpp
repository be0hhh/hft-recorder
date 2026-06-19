#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/replay/EventRows.hpp"

namespace hftrec::arbitrage {

enum class CandleSpreadLegRole : std::uint8_t {
    Unknown = 0,
    Spot = 1,
    Futures = 2,
};

struct CandleSpreadSource {
    std::vector<hftrec::replay::CandleRow> rows{};
    std::string marketHint{};
};

struct CandleSpreadPoint {
    std::int64_t tsNs{0};
    double spreadBps{0.0};
    std::int64_t spotCloseE8{0};
    std::int64_t futuresCloseE8{0};
};

std::vector<hftrec::replay::CandleRow> selectCompareCandles(
    const std::vector<hftrec::replay::CandleRow>& rows);

std::vector<CandleSpreadPoint> buildFuturesPremiumCandleSpread(
    const CandleSpreadSource& a,
    const CandleSpreadSource& b);

}  // namespace hftrec::arbitrage
