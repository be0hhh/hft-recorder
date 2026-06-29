#include "gui/viewer/MoexBasisSeries.hpp"

#include <algorithm>

#include "core/arbitrage/PriceBasis.hpp"

namespace hftrec::gui::viewer {
namespace {

bool validClose(const hftrec::replay::CandleRow& row) noexcept {
    return row.tsNs > 0 && moexBasisClosePriceE8(row) > 0;
}

std::int64_t durationFromTier(std::int64_t tier) noexcept {
    if (tier == 1) return 60LL * 1000000000LL;
    if (tier == 2) return 15LL * 60LL * 1000000000LL;
    if (tier == 3) return 24LL * 60LL * 60LL * 1000000000LL;
    return 60LL * 1000000000LL;
}

std::int64_t candleDurationNs(const hftrec::replay::CandleRow& row) noexcept {
    return row.durationNs > 0 ? row.durationNs : durationFromTier(row.tier);
}

}  // namespace

std::int64_t moexBasisClosePriceE8(const hftrec::replay::CandleRow& row) noexcept {
    if (row.closeE8 > 0) return row.closeE8;
    if (row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) return 0;
    return row.lowE8 + ((row.highE8 - row.lowE8) / 2);
}

std::vector<hftrec::replay::CandleRow> selectMoexBasisCandles(
    const std::vector<hftrec::replay::CandleRow>& rows) {
    std::vector<hftrec::replay::CandleRow> detailed;
    std::vector<hftrec::replay::CandleRow> tierOne;
    detailed.reserve(rows.size());
    tierOne.reserve(rows.size());
    for (const auto& row : rows) {
        if (!validClose(row)) continue;
        if (row.hasOhlc) detailed.push_back(row);
        else if (row.tier == 1) tierOne.push_back(row);
    }
    auto lessTs = [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return moexBasisClosePriceE8(lhs) < moexBasisClosePriceE8(rhs);
    };
    if (!detailed.empty()) {
        std::stable_sort(detailed.begin(), detailed.end(), lessTs);
        return detailed;
    }
    std::stable_sort(tierOne.begin(), tierOne.end(), lessTs);
    return tierOne;
}

std::vector<MoexBasisPoint> buildMoexBasisPoints(const MoexBasisLegSeries& spot,
                                                 const MoexBasisLegSeries& future) {
    std::vector<MoexBasisPoint> out;
    if (future.expiryUtcNs <= 0 || future.priceBasisQtyE8 <= 0) return out;
    out.reserve(std::min(spot.candles.size(), future.candles.size()));

    std::size_t si = 0u;
    std::size_t fi = 0u;
    while (si < spot.candles.size() && fi < future.candles.size()) {
        const auto& spotRow = spot.candles[si];
        const auto& futureRow = future.candles[fi];
        if (spotRow.tsNs < futureRow.tsNs) {
            ++si;
            continue;
        }
        if (futureRow.tsNs < spotRow.tsNs) {
            ++fi;
            continue;
        }

        const std::int64_t spotClose = moexBasisClosePriceE8(spotRow);
        const std::int64_t nativeFutureClose = moexBasisClosePriceE8(futureRow);
        const std::int64_t futureClose =
            hftrec::arbitrage::normalizeNativePriceE8(nativeFutureClose, future.priceBasisQtyE8);
        if (spotClose > 0 && futureClose > 0) {
            MoexBasisPoint point{};
            point.tsNs = spotRow.tsNs;
            point.spotCloseE8 = spotClose;
            point.futureCloseE8 = futureClose;
            point.basisBps = (static_cast<double>(futureClose - spotClose) / static_cast<double>(spotClose)) * 10000.0;
            point.durationNs = std::max(candleDurationNs(spotRow), candleDurationNs(futureRow));
            out.push_back(point);
        }
        ++si;
        ++fi;
    }
    return out;
}

}  // namespace hftrec::gui::viewer
