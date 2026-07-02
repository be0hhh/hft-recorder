#pragma once

#include <cstdint>
#include <string>
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

struct MoexBasisTimeRange {
    std::int64_t minTsNs{0};
    std::int64_t maxTsNs{1};
    bool hasData{false};
};

struct MoexBasisFutureConflictInput {
    std::string symbol{};
    std::int64_t expiryUtcNs{0};
    bool enabled{true};
    bool valid{true};
};

struct MoexBasisFutureConflict {
    std::int64_t expiryUtcNs{0};
    std::vector<std::string> symbols{};
};

std::int64_t moexBasisClosePriceE8(const hftrec::replay::CandleRow& row) noexcept;

std::vector<hftrec::replay::CandleRow> selectMoexBasisCandles(
    const std::vector<hftrec::replay::CandleRow>& rows);

std::vector<MoexBasisPoint> buildMoexBasisPoints(const MoexBasisLegSeries& spot,
                                                 const MoexBasisLegSeries& future);

MoexBasisTimeRange moexBasisLoadedTimeRange(const std::vector<hftrec::replay::CandleRow>& spotCandles,
                                            const std::vector<MoexBasisLegSeries>& futureLegs) noexcept;

std::vector<MoexBasisFutureConflict> findMoexBasisFutureConflicts(
    const std::vector<MoexBasisFutureConflictInput>& futures);

}  // namespace hftrec::gui::viewer
