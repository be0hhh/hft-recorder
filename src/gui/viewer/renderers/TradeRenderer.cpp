#include "gui/viewer/renderers/TradeRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/BookMath.hpp"
#include "gui/viewer/detail/TradeGrouping.hpp"

namespace hftrec::gui::viewer::renderers {

void renderTrades(const RenderContext& ctx) {
    if ((!ctx.s.tradesVisible || ctx.s.tradeDots.empty()) && (!ctx.s.liquidationsVisible || ctx.s.liquidationDots.empty())) return;

    const auto& vp   = ctx.s.vp;
    const auto& dots = ctx.s.tradeDots;
    const bool denseApproximation = ctx.s.tradeDecimated;

    if (ctx.s.tradesVisible && ctx.s.tradeConnectorsVisible) {
        ctx.p->save();
        ctx.p->setRenderHint(QPainter::Antialiasing, false);
        QPen connectorPen(tradeConnectorColor());
        connectorPen.setWidth(1);
        connectorPen.setCapStyle(Qt::SquareCap);
        if (denseApproximation) {
            QColor connector = tradeConnectorColor();
            connector.setAlpha(160);
            connectorPen.setColor(connector);
        }
        ctx.p->setPen(connectorPen);

        QPointF prev;
        int prevOrig = -2;
        for (std::size_t i = 0; i < dots.size(); ++i) {
            const auto& dot = dots[i];
            const int x = static_cast<int>(std::round(vp.toX(dot.tsNs)));
            const int y = static_cast<int>(std::round(vp.toY(dot.priceE8)));
            const QPointF pt{static_cast<qreal>(x), static_cast<qreal>(y)};
            if (prevOrig >= 0 && dot.firstOrigIndex >= 0 && prevOrig + 1 == dot.firstOrigIndex && prev != pt) {
                ctx.p->drawLine(prev, pt);
            }
            prev = pt;
            prevOrig = dot.lastOrigIndex >= 0 ? dot.lastOrigIndex : dot.origIndex;
        }
        ctx.p->restore();
    }

    ctx.p->save();
    ctx.p->setRenderHint(QPainter::Antialiasing, !denseApproximation);
    ctx.p->setPen(Qt::NoPen);
    if (ctx.s.tradesVisible) for (const auto& dot : dots) {
        const qreal x = vp.toX(dot.tsNs);
        const qreal y = vp.toY(dot.priceE8);
        const auto amountE8 = detail::displayTradeAmountE8(dot);
        const qreal radius = detail::amountRadiusScale(amountE8, ctx.s.tradeAmountScale, ctx.s.interactiveMode);
        if ((x + radius) < 0.0 || (x - radius) > vp.w || (y + radius) < 0.0 || (y - radius) > vp.h) continue;
        if ((radius * 2.0) < 1.0) continue;
        QColor fill = dot.sideBuy ? tradeBuyColor() : tradeSellColor();
        fill.setAlpha(255);
        ctx.p->setBrush(fill);
        if (denseApproximation) {
            const int xPx = static_cast<int>(std::round(x));
            const int yPx = static_cast<int>(std::round(y));
            const int diameter = std::max(1, static_cast<int>(std::round(radius * 2.0)));
            const int half = std::max(0, diameter / 2);
            ctx.p->drawRect(xPx - half, yPx - half, diameter, diameter);
        } else {
            ctx.p->drawEllipse(QPointF{x, y}, radius, radius);
        }
    }
    if (ctx.s.liquidationsVisible) {
        ctx.p->setRenderHint(QPainter::Antialiasing, true);
        ctx.p->setPen(Qt::NoPen);
        for (const auto& dot : ctx.s.liquidationDots) {
            const qreal x = vp.toX(dot.tsNs);
            const qreal y = vp.toY(dot.priceE8);
            const auto amountE8 = detail::multiplyScaledE8(dot.qtyE8, dot.priceE8);
            const qreal radius = detail::amountRadiusScale(amountE8, ctx.s.tradeAmountScale, ctx.s.interactiveMode);
            if ((x + radius) < 0.0 || (x - radius) > vp.w || (y + radius) < 0.0 || (y - radius) > vp.h) continue;
            if ((radius * 2.0) < 1.0) continue;
            QColor fill = dot.sideBuy ? QColor(255, 221, 0) : QColor(255, 255, 255);
            fill.setAlpha(255);
            ctx.p->setBrush(fill);
            ctx.p->drawEllipse(QPointF{x, y}, radius, radius);
        }
    }
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
