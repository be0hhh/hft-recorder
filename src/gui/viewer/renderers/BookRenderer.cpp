#include "gui/viewer/renderers/BookRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QPen>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::renderers {

namespace {

// UI slider 0..1 → visible levels budget per side. Default 0.7 ≈ 146 levels.
inline int detailBudget(qreal bookRenderDetail) noexcept {
    const qreal d = std::clamp<qreal>(bookRenderDetail, 0.0, 1.0);
    return 20 + static_cast<int>(std::round(d * 180.0));
}

inline bool inViewport(const BookLevel& l, const ViewportMap& vp) noexcept {
    return l.qtyE8 > 0 && l.priceE8 >= vp.pMin && l.priceE8 <= vp.pMax;
}

void drawSide(QPainter* painter,
              const std::vector<BookLevel>& levels,
              const ViewportMap& vp,
              qreal xLeft, qreal xRight,
              std::int64_t maxQty,
              const QColor& baseColor,
              qreal opacityGain,
              int budget) {
    if (xRight <= xLeft || maxQty <= 0 || levels.empty() || budget <= 0) return;

    opacityGain = detail::clampReal(opacityGain, 0.0, 1.65);
    const int xStart   = static_cast<int>(std::floor(xLeft));
    const int xEnd     = std::max(xStart + 1, static_cast<int>(std::ceil(xRight)));
    const int width    = xEnd - xStart;
    const int heightPx = static_cast<int>(std::ceil(vp.h));

    // Pass 1: flat-alpha bands between adjacent visible levels.
    painter->setPen(Qt::NoPen);
    bool havePrev = false;
    std::int64_t prevPrice = 0;
    std::int64_t prevQty   = 0;
    int seen = 0;
    for (const auto& l : levels) {
        if (seen >= budget) break;
        if (!inViewport(l, vp)) continue;
        ++seen;
        if (havePrev) {
            const int yNear = std::clamp(static_cast<int>(std::round(vp.toY(prevPrice))), 0, heightPx);
            const int yFar  = std::clamp(static_cast<int>(std::round(vp.toY(l.priceE8))), 0, heightPx);
            const int yTop  = std::min(yNear, yFar);
            const int yBot  = std::max(yNear, yFar);
            if (yBot > yTop + 1) {
                const qreal ratio = std::clamp(
                    static_cast<qreal>(prevQty) / static_cast<qreal>(maxQty), 0.0, 1.0);
                QColor fill = baseColor;
                fill.setAlpha(std::clamp(
                    static_cast<int>(std::round(ratio * 60.0 * opacityGain)), 4, 80));
                painter->fillRect(xStart, yTop + 1, width, yBot - yTop - 1, fill);
            }
        }
        prevPrice = l.priceE8;
        prevQty   = l.qtyE8;
        havePrev  = true;
    }

    // Pass 2: 1-px crisp line per level.
    seen = 0;
    for (const auto& l : levels) {
        if (seen >= budget) break;
        if (!inViewport(l, vp)) continue;
        ++seen;
        const int y = std::clamp(static_cast<int>(std::round(vp.toY(l.priceE8))), 0, heightPx - 1);
        const qreal ratio = std::clamp(
            static_cast<qreal>(l.qtyE8) / static_cast<qreal>(maxQty), 0.0, 1.0);
        QColor color = baseColor;
        color.setAlpha(std::clamp(
            static_cast<int>(std::round((28.0 + ratio * 227.0) * opacityGain)), 40, 255));
        QPen pen(color);
        pen.setWidth(1);
        pen.setCapStyle(Qt::SquareCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(xStart, y, xEnd - 1, y);
    }
}

}  // namespace

void renderBook(const RenderContext& ctx) {
    if (!ctx.s.orderbookVisible) return;
    const qreal gain   = detail::remapBookOpacity(ctx.s.bookOpacityGain, ctx.s.interactiveMode);
    const int   budget = detailBudget(ctx.s.bookRenderDetail);

    for (const auto& seg : ctx.s.bookSegments) {
        const qreal xLeft  = std::clamp(ctx.s.vp.toX(seg.tsStartNs), 0.0, ctx.s.vp.w);
        const qreal xRight = std::clamp(ctx.s.vp.toX(seg.tsEndNs),   0.0, ctx.s.vp.w);
        if (xRight <= xLeft) continue;
        drawSide(ctx.p, seg.bids, ctx.s.vp, xLeft, xRight, seg.maxBidQty, bidColor(), gain, budget);
        drawSide(ctx.p, seg.asks, ctx.s.vp, xLeft, xRight, seg.maxAskQty, askColor(), gain, budget);
    }
}

}  // namespace hftrec::gui::viewer::renderers
