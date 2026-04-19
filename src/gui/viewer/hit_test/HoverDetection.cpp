#include "gui/viewer/hit_test/HoverDetection.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::hit_test {

namespace {

// The "active" segment for hover = the last segment with tsStartNs <= cursor.
// Cursor is viewport centre (matches ChartController::viewportCursorTs).
const BookSegment* activeSegment(const RenderSnapshot& snap) noexcept {
    if (snap.bookSegments.empty()) return nullptr;
    const std::int64_t cursor = (snap.vp.tMin + snap.vp.tMax) / 2;
    const BookSegment* best = nullptr;
    for (const auto& seg : snap.bookSegments) {
        if (seg.tsStartNs > cursor) break;
        best = &seg;
    }
    if (!best) best = &snap.bookSegments.front();
    return best;
}

}  // namespace

void computeHover(const RenderSnapshot& snap,
                  const QPointF& point,
                  bool contextActive,
                  HoverInfo& out) {
    out = HoverInfo{};
    out.active        = true;
    out.contextActive = contextActive;
    out.point         = point;

    if (!snap.loaded) { out.active = false; return; }
    const auto& vp = snap.vp;
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin || vp.w <= 0.0 || vp.h <= 0.0) {
        out.active = false;
        return;
    }

    // Book / ticker hit-test.
    if (snap.orderbookVisible || snap.bookTickerVisible) {
        if (const auto* seg = activeSegment(snap); seg != nullptr) {
            constexpr double kTickerHitPx = 6.0;
            const double bidDist = seg->tickerBidE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(seg->tickerBidE8) - point.y());
            const double askDist = seg->tickerAskE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(seg->tickerAskE8) - point.y());

            if (bidDist <= kTickerHitPx || askDist <= kTickerHitPx) {
                const bool bidWins = bidDist <= askDist;
                out.bookKind    = bidWins ? 1 : 2;
                out.bookPriceE8 = bidWins ? seg->tickerBidE8 : seg->tickerAskE8;
                out.bookQtyE8   = bidWins ? seg->tickerBidQtyE8 : seg->tickerAskQtyE8;
                out.bookTsNs    = seg->tsStartNs;
            } else if (snap.orderbookVisible) {
                constexpr double kBookHitPx = 8.0;
                std::int64_t priceE8 = 0;
                std::int64_t qtyE8   = 0;
                if (detail::findNearestBookLevel(
                        seg->bids, vp, point.y(), kBookHitPx, priceE8, qtyE8)) {
                    out.bookKind    = 3;
                    out.bookPriceE8 = priceE8;
                    out.bookQtyE8   = qtyE8;
                    out.bookTsNs    = seg->tsStartNs;
                }

                std::int64_t askPriceE8 = 0;
                std::int64_t askQtyE8   = 0;
                if (detail::findNearestBookLevel(
                        seg->asks, vp, point.y(), kBookHitPx, askPriceE8, askQtyE8)) {
                    const double askDistPx = std::abs(vp.toY(askPriceE8) - point.y());
                    const double curDistPx = out.bookKind == 3
                        ? std::abs(vp.toY(out.bookPriceE8) - point.y())
                        : std::numeric_limits<double>::max();
                    if (askDistPx <= curDistPx) {
                        out.bookKind    = 4;
                        out.bookPriceE8 = askPriceE8;
                        out.bookQtyE8   = askQtyE8;
                        out.bookTsNs    = seg->tsStartNs;
                    }
                }
            }
        }
    }

    // Trade hit-test.
    if (!snap.tradesVisible) return;

    constexpr double kHitRadiusPx = 9.0;
    const double hitSq = kHitRadiusPx * kHitRadiusPx;
    double bestSq = hitSq;
    for (const auto& dot : snap.tradeDots) {
        const double dx = vp.toX(dot.tsNs) - point.x();
        const double dy = vp.toY(dot.priceE8) - point.y();
        const double distSq = dx * dx + dy * dy;
        if (distSq <= bestSq) {
            bestSq             = distSq;
            out.tradeHit       = true;
            out.tradeOrigIndex = dot.origIndex;
            out.tradeTsNs      = dot.tsNs;
            out.tradePriceE8   = dot.priceE8;
            out.tradeQtyE8     = dot.qtyE8;
            out.tradeSideBuy   = dot.sideBuy;
        }
    }
}

}  // namespace hftrec::gui::viewer::hit_test
