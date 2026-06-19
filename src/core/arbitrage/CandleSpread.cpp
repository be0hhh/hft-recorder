#include "core/arbitrage/CandleSpread.hpp"

#include <algorithm>
#include <string_view>

namespace hftrec::arbitrage {

namespace {

std::string toLowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

bool eqAscii(std::string_view lhs, std::string_view rhs) noexcept {
    return lhs == rhs;
}

bool containsAscii(std::string_view text, std::string_view needle) noexcept {
    return text.find(needle) != std::string_view::npos;
}

CandleSpreadLegRole roleFromMarket(std::string_view market) {
    const std::string lowered = toLowerAscii(market);
    if (eqAscii(lowered, "spot") || eqAscii(lowered, "shares")) return CandleSpreadLegRole::Spot;
    if (eqAscii(lowered, "futures") || eqAscii(lowered, "forts") || eqAscii(lowered, "swap")) return CandleSpreadLegRole::Futures;
    if (containsAscii(lowered, "spot") || containsAscii(lowered, "share")) return CandleSpreadLegRole::Spot;
    if (containsAscii(lowered, "future") || containsAscii(lowered, "forts") || containsAscii(lowered, "swap")) return CandleSpreadLegRole::Futures;
    return CandleSpreadLegRole::Unknown;
}

CandleSpreadLegRole roleForSource(const CandleSpreadSource& source) {
    if (const auto role = roleFromMarket(source.marketHint); role != CandleSpreadLegRole::Unknown) return role;
    for (const auto& row : source.rows) {
        if (const auto role = roleFromMarket(row.market); role != CandleSpreadLegRole::Unknown) return role;
    }
    return CandleSpreadLegRole::Unknown;
}

std::int64_t closePriceE8(const hftrec::replay::CandleRow& row) noexcept {
    if (row.closeE8 > 0) return row.closeE8;
    if (row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) return 0;
    return row.lowE8 + ((row.highE8 - row.lowE8) / 2);
}

bool validCandlePrice(const hftrec::replay::CandleRow& row) noexcept {
    return row.tsNs > 0 && closePriceE8(row) > 0;
}

CandleSpreadPoint makePoint(std::int64_t tsNs,
                            const hftrec::replay::CandleRow& spot,
                            const hftrec::replay::CandleRow& futures) noexcept {
    const std::int64_t spotClose = closePriceE8(spot);
    const std::int64_t futuresClose = closePriceE8(futures);
    CandleSpreadPoint out{};
    out.tsNs = tsNs;
    out.spotCloseE8 = spotClose;
    out.futuresCloseE8 = futuresClose;
    if (spotClose > 0 && futuresClose > 0) {
        out.spreadBps = (static_cast<double>(futuresClose - spotClose) / static_cast<double>(spotClose)) * 10000.0;
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

std::vector<CandleSpreadPoint> buildFuturesPremiumCandleSpread(
    const CandleSpreadSource& a,
    const CandleSpreadSource& b) {
    const CandleSpreadLegRole roleA = roleForSource(a);
    const CandleSpreadLegRole roleB = roleForSource(b);
    if (roleA == roleB || roleA == CandleSpreadLegRole::Unknown || roleB == CandleSpreadLegRole::Unknown) return {};

    const auto& spotRows = roleA == CandleSpreadLegRole::Spot ? a.rows : b.rows;
    const auto& futuresRows = roleA == CandleSpreadLegRole::Futures ? a.rows : b.rows;
    std::vector<CandleSpreadPoint> out;
    out.reserve(spotRows.size() + futuresRows.size());

    std::size_t si = 0u;
    std::size_t fi = 0u;
    while (si < spotRows.size() && fi < futuresRows.size()) {
        if (spotRows[si].tsNs < futuresRows[fi].tsNs) {
            ++si;
            continue;
        }
        if (futuresRows[fi].tsNs < spotRows[si].tsNs) {
            ++fi;
            continue;
        }
        if (validCandlePrice(spotRows[si]) && validCandlePrice(futuresRows[fi])) {
            out.push_back(makePoint(spotRows[si].tsNs, spotRows[si], futuresRows[fi]));
        }
        ++si;
        ++fi;
    }
    return out;
}

}  // namespace hftrec::arbitrage
