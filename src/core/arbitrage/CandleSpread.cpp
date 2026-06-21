#include "core/arbitrage/CandleSpread.hpp"

#include <algorithm>

namespace hftrec::arbitrage {

namespace {

std::int64_t closePriceE8(const hftrec::replay::CandleRow& row) noexcept {
    if (row.closeE8 > 0) return row.closeE8;
    if (row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) return 0;
    return row.lowE8 + ((row.highE8 - row.lowE8) / 2);
}

std::int64_t normalizedClosePriceE8(const hftrec::replay::CandleRow& row,
                                    std::int64_t priceBasisQtyE8) noexcept {
    return normalizeNativePriceE8(closePriceE8(row), priceBasisQtyE8);
}

bool validCandlePrice(const hftrec::replay::CandleRow& row) noexcept {
    return row.tsNs > 0 && closePriceE8(row) > 0;
}

std::int64_t durationFromTier(std::int64_t tier) noexcept {
    if (tier == 1) return 60ll * 1000000000ll;
    if (tier == 2) return 15ll * 60ll * 1000000000ll;
    if (tier == 3) return 24ll * 60ll * 60ll * 1000000000ll;
    return 60ll * 1000000000ll;
}

std::int64_t candleDurationNs(const hftrec::replay::CandleRow& row) noexcept {
    return row.durationNs > 0 ? row.durationNs : durationFromTier(row.tier);
}

CandleSpreadPoint makePoint(const hftrec::replay::CandleRow& a,
                            const hftrec::replay::CandleRow& b,
                            std::int64_t aPriceBasisQtyE8,
                            std::int64_t bPriceBasisQtyE8) noexcept {
    const std::int64_t aClose = normalizedClosePriceE8(a, aPriceBasisQtyE8);
    const std::int64_t bClose = normalizedClosePriceE8(b, bPriceBasisQtyE8);
    CandleSpreadPoint out{};
    out.tsNs = a.tsNs;
    out.aCloseE8 = aClose;
    out.bCloseE8 = bClose;
    out.durationNs = std::max(candleDurationNs(a), candleDurationNs(b));
    if (aClose > 0 && bClose > 0) {
        if (bClose > aClose) {
            out.direction = SpreadDirection::BuyAAskSellBBid;
            out.spreadBps = (static_cast<double>(bClose - aClose) / static_cast<double>(aClose)) * 10000.0;
        } else if (aClose > bClose) {
            out.direction = SpreadDirection::BuyBAskSellABid;
            out.spreadBps = (static_cast<double>(aClose - bClose) / static_cast<double>(bClose)) * 10000.0;
        }
    }
    return out;
}

}  // namespace

std::vector<hftrec::replay::CandleRow> selectCompareCandles(
    const std::vector<hftrec::replay::CandleRow>& rows) {
    std::vector<hftrec::replay::CandleRow> detailed;
    std::vector<hftrec::replay::CandleRow> tierOne;
    detailed.reserve(rows.size());
    tierOne.reserve(rows.size());
    for (const auto& row : rows) {
        if (!validCandlePrice(row)) continue;
        if (row.hasOhlc) {
            detailed.push_back(row);
        } else if (row.tier == 1) {
            tierOne.push_back(row);
        }
    }
    auto lessTs = [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return closePriceE8(lhs) < closePriceE8(rhs);
    };
    if (!detailed.empty()) {
        std::stable_sort(detailed.begin(), detailed.end(), lessTs);
        return detailed;
    }
    std::stable_sort(tierOne.begin(), tierOne.end(), lessTs);
    return tierOne;
}

std::vector<CandleSpreadPoint> buildBestSideCandleSpread(
    const CandleSpreadSource& a,
    const CandleSpreadSource& b) {
    std::vector<CandleSpreadPoint> out;
    out.reserve(std::min(a.rows.size(), b.rows.size()));

    std::size_t ai = 0u;
    std::size_t bi = 0u;
    while (ai < a.rows.size() && bi < b.rows.size()) {
        if (a.rows[ai].tsNs < b.rows[bi].tsNs) {
            ++ai;
            continue;
        }
        if (b.rows[bi].tsNs < a.rows[ai].tsNs) {
            ++bi;
            continue;
        }
        if (validCandlePrice(a.rows[ai]) && validCandlePrice(b.rows[bi])) {
            out.push_back(makePoint(a.rows[ai], b.rows[bi], a.priceBasisQtyE8, b.priceBasisQtyE8));
        }
        ++ai;
        ++bi;
    }
    return out;
}

}  // namespace hftrec::arbitrage
