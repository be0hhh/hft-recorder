#pragma once

#include <cstdint>
#include <vector>

#include "core/replay/EventRows.hpp"

namespace hftrec::gui::viewer {

struct MoexBasisLegSeries {
    std::vector<hftrec::replay::CandleRow> candles{};
    std::int64_t priceBasisQtyE8{0};
    std::int64_t expiryUtcNs{0};
};

struct MoexBasisPoint {
    std::int64_t tsNs{0};
    std::int64_t spotCloseE8{0};
    std::int64_t futureCloseE8{0};
    double basisBps{0.0};
    std::int64_t durationNs{0};
};

std::int64_t moexBasisClosePriceE8(const hftrec::replay::CandleRow& row) noexcept;

std::vector<hftrec::replay::CandleRow> selectMoexBasisCandles(
    const std::vector<hftrec::replay::CandleRow>& rows);

std::vector<MoexBasisPoint> buildMoexBasisPoints(const MoexBasisLegSeries& spot,
                                                 const MoexBasisLegSeries& future);

}  // namespace hftrec::gui::viewer
