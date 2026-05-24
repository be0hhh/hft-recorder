#include "gui/viewer/renderers/StrategyOverlayRenderer.hpp"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"

namespace hftrec::gui::viewer::renderers {

void renderStrategyOverlay(const RenderContext& ctx) {
    if (ctx.s.strategyOrderSegments.empty() && ctx.s.strategyFillMarkers.empty()) return;

    const auto& vp = ctx.s.vp;
    ctx.p->save();

    ctx.p->setRenderHint(QPainter::Antialiasing, false);
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
    markerOutline.setWidth(1);
    markerOutline.setCosmetic(true);
    ctx.p->setPen(markerOutline);
    for (const auto& marker : ctx.s.strategyFillMarkers) {
        const qreal x = vp.toX(marker.tsNs);
        const qreal y = vp.toY(marker.priceE8);
        if (x < -12.0 || x > vp.w + 12.0 || y < -12.0 || y > vp.h + 12.0) continue;

        QColor fill = marker.sideBuy ? QColor(0xFF, 0xD8, 0x4D) : QColor(0xFF, 0x1A, 0xC8);
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
