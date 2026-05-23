#include "gui/viewer/renderers/BookRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QPen>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"

namespace hftrec::gui::viewer::renderers {

namespace {

void drawSide(QPainter* painter,
              const std::vector<BookLevel>& levels,
              const ViewportMap& vp,
              qreal xLeft, qreal xRight,
              const QColor& baseColor) {
    if (xRight <= xLeft || levels.empty()) return;

    const int xStart   = static_cast<int>(std::floor(xLeft));
    const int xEnd     = std::max(xStart + 1, static_cast<int>(std::ceil(xRight)));
    const int width    = xEnd - xStart;
    const int heightPx = static_cast<int>(std::ceil(vp.h));
    if (width < 1 || heightPx <= 0) return;

    int lastDrawnY = std::numeric_limits<int>::min();
    for (const auto& level : levels) {
        if (level.qtyE8 <= 0 || level.priceE8 < vp.pMin || level.priceE8 > vp.pMax) continue;
        if (level.alpha <= 1) continue;

        const int y = std::clamp(static_cast<int>(std::round(vp.toY(level.priceE8))), 0, heightPx - 1);
        if (y == lastDrawnY) continue;
        QColor color = baseColor;
        color.setAlpha(level.alpha);
        QPen pen(color);
        pen.setWidth(1);
        pen.setCapStyle(Qt::SquareCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(xStart, y, xEnd - 1, y);
        lastDrawnY = y;
    }
}

}  // namespace

void renderBook(const RenderContext& ctx) {
    if (!ctx.s.orderbookVisible) return;

    for (const auto& seg : ctx.s.bookSegments) {
        const qreal xLeft  = std::clamp(ctx.s.vp.toX(seg.tsStartNs), 0.0, ctx.s.vp.w);
        const qreal xRight = std::clamp(ctx.s.vp.toX(seg.tsEndNs),   0.0, ctx.s.vp.w);
        if (xRight <= xLeft) continue;

        drawSide(ctx.p, seg.bids, ctx.s.vp, xLeft, xRight, bidColor());
        drawSide(ctx.p, seg.asks, ctx.s.vp, xLeft, xRight, askColor());
    }
}

}  // namespace hftrec::gui::viewer::renderers
