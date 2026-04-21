#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer::detail {

// Max qty among levels with price in [priceMin, priceMax]. MapT iterates
// (price, qty) pairs — works on both std::map<> and std::vector<BookLevel>
// with an adapter below.
template <typename MapT>
std::int64_t maxVisibleQty(const MapT& levels, std::int64_t priceMin, std::int64_t priceMax) {
    std::int64_t maxQty = 0;
    for (const auto& [price, qty] : levels) {
        if (price < priceMin || price > priceMax) continue;
        maxQty = std::max(maxQty, qty);
    }
    return maxQty;
}

inline std::int64_t maxVisibleQty(const std::vector<BookLevel>& levels,
                                  std::int64_t priceMin, std::int64_t priceMax) {
    std::int64_t maxQty = 0;
    for (const auto& l : levels) {
        if (l.priceE8 < priceMin || l.priceE8 > priceMax) continue;
        maxQty = std::max(maxQty, l.qtyE8);
    }
    return maxQty;
}

template <typename MapT>
bool findNearestBookLevel(const MapT& levels,
                          const ViewportMap& vp,
                          qreal hoverY,
                          double maxDistancePx,
                          std::int64_t& outPriceE8,
                          std::int64_t& outQtyE8) {
    double bestDistancePx = maxDistancePx;
    bool found = false;
    for (const auto& [price, qty] : levels) {
        if (price < vp.pMin || price > vp.pMax || qty <= 0) continue;
        const double distancePx = std::abs(vp.toY(price) - hoverY);
        if (distancePx <= bestDistancePx) {
            bestDistancePx = distancePx;
            outPriceE8 = price;
            outQtyE8 = qty;
            found = true;
        }
    }
    return found;
}

inline bool findNearestBookLevel(const std::vector<BookLevel>& levels,
                                 const ViewportMap& vp,
                                 qreal hoverY,
                                 double maxDistancePx,
                                 std::int64_t& outPriceE8,
                                 std::int64_t& outQtyE8) {
    double bestDistancePx = maxDistancePx;
    bool found = false;
    for (const auto& l : levels) {
        if (l.priceE8 < vp.pMin || l.priceE8 > vp.pMax || l.qtyE8 <= 0) continue;
        const double distancePx = std::abs(vp.toY(l.priceE8) - hoverY);
        if (distancePx <= bestDistancePx) {
            bestDistancePx = distancePx;
            outPriceE8 = l.priceE8;
            outQtyE8 = l.qtyE8;
            found = true;
        }
    }
    return found;
}

inline qreal amountRadiusScale(std::int64_t amountE8, qreal amountScale, bool interactiveMode) {
    const auto amountAbs = amountE8 < 0
        ? static_cast<double>(static_cast<std::uint64_t>(-(amountE8 + 1)) + 1u)
        : static_cast<double>(static_cast<std::uint64_t>(amountE8));
    const double normalized = std::log10(1.0 + amountAbs / 100000000.0);
    const qreal baseRadius = interactiveMode ? 0.45 : 0.5;
    const qreal gain = interactiveMode ? 0.7 : 1.1;
    return clampReal(baseRadius + static_cast<qreal>(normalized) * amountScale * gain,
                     0.5,
                     interactiveMode ? 1.2 : 1.7);
}

inline qreal remapBookOpacity(qreal gain, bool interactiveMode) {
    gain = clampReal(gain, 0.0, 1.0);
    const qreal curved = std::pow(gain, 0.55);
    return interactiveMode ? (0.04 + curved * 1.05) : (0.02 + curved * 1.55);
}

}  // namespace hftrec::gui::viewer::detail
