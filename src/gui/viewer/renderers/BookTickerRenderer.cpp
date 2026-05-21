#include "gui/viewer/renderers/BookTickerRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"

namespace hftrec::gui::viewer::renderers {

namespace {

void drawSampleSide(QPainter* painter,
                    const BookTickerTrace& trace,
                    const ViewportMap& vp,
                    const QPen& pen,
                    bool bidSide) {
    if (trace.samples.empty()) return;
    painter->setPen(pen);

    bool prevVisible = false;
    QPointF prevPoint{};
    for (const auto& sample : trace.samples) {
        const std::int64_t priceE8 = bidSide ? sample.bidPriceE8 : sample.askPriceE8;
        if (priceE8 <= 0 || priceE8 < vp.pMin || priceE8 > vp.pMax
            || sample.tsNs < vp.tMin || sample.tsNs > vp.tMax) {
            prevVisible = false;
            continue;
        }

        const qreal x = vp.toX(sample.tsNs);
        const qreal y = vp.toY(priceE8);
        if (x < 0.0 || x > vp.w || y < 0.0 || y > vp.h) {
            prevVisible = false;
            continue;
        }

        const int xPx = static_cast<int>(std::round(x));
        const int yPx = static_cast<int>(std::round(y));
        const QPointF point{static_cast<qreal>(xPx), static_cast<qreal>(yPx)};
        if (prevVisible) painter->drawLine(prevPoint, point);
        else painter->drawLine(point, QPointF{static_cast<qreal>(xPx + 1), static_cast<qreal>(yPx)});
        prevPoint = point;
        prevVisible = true;
    }
}

}  // namespace

void renderBookTicker(const RenderContext& ctx) {
    if (!ctx.s.bookTickerVisible) return;
    if (ctx.s.bookTickerTrace.samples.empty()) return;

    QColor bidCol = bidColor(); bidCol.setAlpha(255);
    QColor askCol = askColor(); askCol.setAlpha(255);

    QPen bidPen(bidCol);
    bidPen.setWidth(1);
    bidPen.setCapStyle(Qt::SquareCap);
    bidPen.setJoinStyle(Qt::MiterJoin);

    QPen askPen(askCol);
    askPen.setWidth(1);
    askPen.setCapStyle(Qt::SquareCap);
    askPen.setJoinStyle(Qt::MiterJoin);

    ctx.p->save();
    ctx.p->setRenderHint(QPainter::Antialiasing, false);
    ctx.p->setBrush(Qt::NoBrush);
    drawSampleSide(ctx.p, ctx.s.bookTickerTrace, ctx.s.vp, bidPen, true);
    drawSampleSide(ctx.p, ctx.s.bookTickerTrace, ctx.s.vp, askPen, false);
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
