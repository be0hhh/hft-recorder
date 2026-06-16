#include "gui/viewer/renderers/StrategyOverlayRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"

namespace hftrec::gui::viewer::renderers {
namespace {

void drawRangeLine(const RenderContext& ctx,
                   const std::vector<StrategyRangePoint>& points,
                   int lane,
                   const QPen& pen) {
    if (points.size() < 2u) return;
    const auto& vp = ctx.s.vp;
    ctx.p->setPen(pen);
    for (std::size_t i = 1u; i < points.size(); ++i) {
        const StrategyRangePoint& prev = points[i - 1u];
        const StrategyRangePoint& cur = points[i];
        std::int64_t prevPrice = prev.midE8;
        std::int64_t curPrice = cur.midE8;
        if (lane == 0) {
            prevPrice = prev.lowE8;
            curPrice = cur.lowE8;
        } else if (lane == 2) {
            prevPrice = prev.highE8;
            curPrice = cur.highE8;
        }
        if (prevPrice <= 0 || curPrice <= 0) continue;
        const qreal x0 = std::clamp(vp.toX(prev.tsNs), -8.0, vp.w + 8.0);
        const qreal x1 = std::clamp(vp.toX(cur.tsNs), -8.0, vp.w + 8.0);
        if (std::abs(x1 - x0) < 1.0) continue;
        const qreal y0 = vp.toY(prevPrice);
        const qreal y1 = vp.toY(curPrice);
        if ((y0 < -4.0 && y1 < -4.0) || (y0 > vp.h + 4.0 && y1 > vp.h + 4.0)) continue;
        ctx.p->drawLine(QPointF{x0, y0}, QPointF{x1, y1});
    }
}

}  // namespace

void renderStrategyOverlay(const RenderContext& ctx) {
    if (ctx.s.strategyOrderSegments.empty() && ctx.s.strategyFillMarkers.empty() && ctx.s.strategyRangePoints.empty()) return;

    const auto& vp = ctx.s.vp;
    ctx.p->save();

    ctx.p->setRenderHint(QPainter::Antialiasing, false);
    if (!ctx.s.strategyRangePoints.empty()) {
        QPen edgePen(QColor(0x7D, 0xB7, 0xD8, 0x96));
        edgePen.setWidth(1);
        edgePen.setCosmetic(true);
        drawRangeLine(ctx, ctx.s.strategyRangePoints, 0, edgePen);
        drawRangeLine(ctx, ctx.s.strategyRangePoints, 2, edgePen);

        QPen midPen(QColor(0xFF, 0xD1, 0x66, 0xB8));
        midPen.setWidth(1);
        midPen.setCosmetic(true);
        midPen.setStyle(Qt::DashLine);
        drawRangeLine(ctx, ctx.s.strategyRangePoints, 1, midPen);
    }

    for (const auto& segment : ctx.s.strategyOrderSegments) {
        const qreal y = vp.toY(segment.priceE8);
        if (y < -2.0 || y > vp.h + 2.0) continue;
        const qreal x0 = std::clamp(vp.toX(segment.tsStartNs), -8.0, vp.w + 8.0);
        const qreal x1 = std::clamp(vp.toX(segment.tsEndNs), -8.0, vp.w + 8.0);
        if (std::abs(x1 - x0) < 1.0) continue;

        QColor color = segment.sideBuy ? tradeBuyColor() : tradeSellColor();
        color.setAlpha(230);
        QPen pen(color);
        pen.setWidth(2);
        pen.setCapStyle(Qt::SquareCap);
        ctx.p->setPen(pen);
        ctx.p->drawLine(QPointF{x0, y}, QPointF{x1, y});
    }

    ctx.p->setRenderHint(QPainter::Antialiasing, true);
    QPen markerOutline(QColor(0x08, 0x08, 0x08, 0xE0));
    markerOutline.setWidth(2);
    markerOutline.setCosmetic(true);
    ctx.p->setPen(markerOutline);
    for (const auto& marker : ctx.s.strategyFillMarkers) {
        const qreal x = vp.toX(marker.tsNs);
        const qreal y = vp.toY(marker.priceE8);
        if (x < -12.0 || x > vp.w + 12.0 || y < -12.0 || y > vp.h + 12.0) continue;

        QColor fill = marker.sideBuy ? QColor(0xFF, 0xE6, 0x6D) : QColor(0xC8, 0x5A, 0x12);
        fill.setAlpha(255);
        ctx.p->setBrush(fill);
        QPolygonF triangle;
        if (marker.shape == StrategyFillShape::BuyUp) {
            triangle << QPointF{x, y} << QPointF{x - 5.0, y + 9.0} << QPointF{x + 5.0, y + 9.0};
        } else {
            triangle << QPointF{x, y} << QPointF{x - 5.0, y - 9.0} << QPointF{x + 5.0, y - 9.0};
        }
        ctx.p->drawPolygon(triangle);
    }

    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
