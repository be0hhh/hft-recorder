#include "gui/viewer/hit_test/HoverDetection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::hit_test {

namespace {

std::int64_t timestampAtX(const ViewportMap& vp, qreal x) noexcept {
    if (vp.tMax <= vp.tMin || vp.w <= 0.0) return vp.tMin;
    const double clampedX = std::clamp(static_cast<double>(x), 0.0, vp.w);
    const double span = static_cast<double>(vp.tMax - vp.tMin);
    return vp.tMin + static_cast<std::int64_t>(std::llround((clampedX / vp.w) * span));
}

std::ptrdiff_t activeSegmentIndexAt(const RenderSnapshot& snap, qreal x) noexcept {
    if (snap.bookSegments.empty()) return -1;
    const std::int64_t cursor = timestampAtX(snap.vp, x);
    const auto it = std::upper_bound(
        snap.bookSegments.begin(),
        snap.bookSegments.end(),
        cursor,
        [](std::int64_t ts, const BookSegment& seg) noexcept { return ts < seg.tsStartNs; });
    if (it == snap.bookSegments.begin()) return -1;
    const auto index = static_cast<std::ptrdiff_t>((it - snap.bookSegments.begin()) - 1);
    const auto& seg = snap.bookSegments[static_cast<std::size_t>(index)];
    if (cursor < seg.tsStartNs || cursor > seg.tsEndNs) return -1;
    return index;
}

std::int64_t minVisibleAmountE8(const RenderSnapshot& snap) noexcept {
    constexpr std::int64_t kUsdScaleE8 = 100000000ll;
    const qreal clampedUsd = std::clamp<qreal>(snap.bookRenderDetail, 1000.0, 1000000.0);
    return static_cast<std::int64_t>(std::llround(clampedUsd * static_cast<qreal>(kUsdScaleE8)));
}

const BookLevel* findVisibleLevelByPrice(const std::vector<BookLevel>& levels,
                                         const ViewportMap& vp,
                                         std::int64_t minVisibleLevelAmountE8,
                                         std::int64_t priceE8) noexcept {
    for (const auto& level : levels) {
        if (level.priceE8 != priceE8) continue;
        if (level.qtyE8 <= 0 || level.priceE8 < vp.pMin || level.priceE8 > vp.pMax) return nullptr;
        const auto amountE8 = detail::multiplyScaledE8(level.qtyE8, level.priceE8);
        return amountE8 >= minVisibleLevelAmountE8 ? &level : nullptr;
    }
    return nullptr;
}

void expandBookSpan(const RenderSnapshot& snap,
                    std::ptrdiff_t segmentIndex,
                    bool bidSide,
                    std::int64_t priceE8,
                    std::int64_t& outStartNs,
                    std::int64_t& outEndNs) noexcept {
    const auto& segments = snap.bookSegments;
    const auto minVisibleLevelAmountE8 = minVisibleAmountE8(snap);
    outStartNs = segments[static_cast<std::size_t>(segmentIndex)].tsStartNs;
    outEndNs = segments[static_cast<std::size_t>(segmentIndex)].tsEndNs;

    for (std::ptrdiff_t i = segmentIndex - 1; i >= 0; --i) {
        const auto& seg = segments[static_cast<std::size_t>(i)];
        const auto& levels = bidSide ? seg.bids : seg.asks;
        if (findVisibleLevelByPrice(levels, snap.vp, minVisibleLevelAmountE8, priceE8) == nullptr) break;
        outStartNs = seg.tsStartNs;
    }
    for (std::ptrdiff_t i = segmentIndex + 1; i < static_cast<std::ptrdiff_t>(segments.size()); ++i) {
        const auto& seg = segments[static_cast<std::size_t>(i)];
        const auto& levels = bidSide ? seg.bids : seg.asks;
        if (findVisibleLevelByPrice(levels, snap.vp, minVisibleLevelAmountE8, priceE8) == nullptr) break;
        outEndNs = seg.tsEndNs;
    }
}

const BookTickerSample* nearestBookTickerSample(const RenderSnapshot& snap, qreal x) noexcept {
    if (snap.bookTickerTrace.samples.empty()) return nullptr;
    const int xPx = std::clamp(
        static_cast<int>(std::floor(x)),
        0,
        std::max(0, static_cast<int>(std::ceil(snap.vp.w)) - 1));
    const auto& samples = snap.bookTickerTrace.samples;
    const auto it = std::lower_bound(
        samples.begin(),
        samples.end(),
        xPx,
        [](const BookTickerSample& sample, int targetX) noexcept {
            return sample.xPx < targetX;
        });

    const BookTickerSample* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    if (it != samples.end()) {
        best = &*it;
        bestDistance = std::abs(it->xPx - xPx);
    }
    if (it != samples.begin()) {
        const auto* prev = &*std::prev(it);
        const int distance = std::abs(prev->xPx - xPx);
        if (distance < bestDistance) {
            best = prev;
            bestDistance = distance;
        }
    }
    return bestDistance <= 6 ? best : nullptr;
}

}  // namespace

void computeHover(const RenderSnapshot& snap,
                  const QPointF& point,
                  bool contextActive,
                  HoverInfo& out) {
    out = HoverInfo{};
    out.active = true;
    out.contextActive = contextActive;
    out.point = point;

    if (!snap.loaded) {
        out.active = false;
        return;
    }
    const auto& vp = snap.vp;
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin || vp.w <= 0.0 || vp.h <= 0.0) {
        out.active = false;
        return;
    }

    if (snap.bookTickerVisible) {
        if (const auto* sample = nearestBookTickerSample(snap, point.x()); sample != nullptr) {
            constexpr double kTickerHitPx = 6.0;
            const double bidDist = sample->bidPriceE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(sample->bidPriceE8) - point.y());
            const double askDist = sample->askPriceE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(sample->askPriceE8) - point.y());

            if (bidDist <= kTickerHitPx || askDist <= kTickerHitPx) {
                const bool bidWins = bidDist <= askDist;
                out.bookKind = bidWins ? 1 : 2;
                out.bookPriceE8 = bidWins ? sample->bidPriceE8 : sample->askPriceE8;
                out.bookQtyE8 = bidWins ? sample->bidQtyE8 : sample->askQtyE8;
                out.bookTsNs = sample->tsNs;
            }
        }
    }

    if (out.bookKind == 0 && snap.orderbookVisible) {
        const auto segIndex = activeSegmentIndexAt(snap, point.x());
        if (segIndex >= 0) {
            const auto* seg = &snap.bookSegments[static_cast<std::size_t>(segIndex)];
            constexpr double kBookHitPx = 8.0;
            std::int64_t priceE8 = 0;
            std::int64_t qtyE8 = 0;
            if (detail::findNearestBookLevel(seg->bids, vp, point.y(), kBookHitPx, priceE8, qtyE8)) {
                out.bookKind = 3;
                out.bookPriceE8 = priceE8;
                out.bookQtyE8 = qtyE8;
                out.bookTsNs = std::clamp(timestampAtX(vp, point.x()), seg->tsStartNs, seg->tsEndNs);
                expandBookSpan(snap, segIndex, true, priceE8, out.bookTsStartNs, out.bookTsEndNs);
            }

            std::int64_t askPriceE8 = 0;
            std::int64_t askQtyE8 = 0;
            if (detail::findNearestBookLevel(seg->asks, vp, point.y(), kBookHitPx, askPriceE8, askQtyE8)) {
                const double askDistPx = std::abs(vp.toY(askPriceE8) - point.y());
                const double curDistPx = out.bookKind == 3
                    ? std::abs(vp.toY(out.bookPriceE8) - point.y())
                    : std::numeric_limits<double>::max();
                if (askDistPx <= curDistPx) {
                    out.bookKind = 4;
                    out.bookPriceE8 = askPriceE8;
                    out.bookQtyE8 = askQtyE8;
                    out.bookTsNs = std::clamp(timestampAtX(vp, point.x()), seg->tsStartNs, seg->tsEndNs);
                    expandBookSpan(snap, segIndex, false, askPriceE8, out.bookTsStartNs, out.bookTsEndNs);
                }
            }
        }
    }

    if (snap.liquidationsVisible) {
        constexpr double kHitRadiusPx = 9.0;
        const double hitSq = kHitRadiusPx * kHitRadiusPx;
        double bestSq = hitSq;
        for (const auto& dot : snap.liquidationDots) {
            const double x = vp.toX(dot.tsNs);
            if (x < (point.x() - kHitRadiusPx)) continue;
            if (x > (point.x() + kHitRadiusPx)) break;
            const double dx = x - point.x();
            const double dy = vp.toY(dot.priceE8) - point.y();
            const double distSq = dx * dx + dy * dy;
            if (distSq <= bestSq) {
                bestSq = distSq;
                out.liquidationHit = true;
                out.liquidationOrigIndex = dot.origIndex;
                out.liquidationTsNs = dot.tsNs;
                out.liquidationPriceE8 = dot.priceE8;
                out.liquidationQtyE8 = dot.qtyE8;
                out.liquidationAvgPriceE8 = dot.avgPriceE8;
                out.liquidationFilledQtyE8 = dot.filledQtyE8;
                out.liquidationSideBuy = dot.sideBuy;
            }
        }
        if (out.liquidationHit) return;
    }

    if (!snap.tradesVisible) return;

    constexpr double kHitRadiusPx = 9.0;
    const double hitSq = kHitRadiusPx * kHitRadiusPx;
    double bestSq = hitSq;
    for (const auto& dot : snap.tradeDots) {
        const double x = vp.toX(dot.tsNs);
        if (x < (point.x() - kHitRadiusPx)) continue;
        if (x > (point.x() + kHitRadiusPx)) break;
        const double dx = x - point.x();
        const double dy = vp.toY(dot.priceE8) - point.y();
        const double distSq = dx * dx + dy * dy;
        if (distSq <= bestSq) {
            bestSq = distSq;
            out.tradeHit = true;
            out.tradeOrigIndex = dot.origIndex;
            out.tradeTsNs = dot.tsNs;
            out.tradePriceE8 = dot.priceE8;
            out.tradeQtyE8 = dot.qtyE8;
            out.tradeSideBuy = dot.sideBuy;
        }
    }
}

}  // namespace hftrec::gui::viewer::hit_test
