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

bool sampleSidePoint(const RenderContext& ctx,
                     const BookTickerSample& sample,
                     bool bidSide,
                     QPointF& out) noexcept {
    const auto price = bidSide ? sample.bidPriceE8 : sample.askPriceE8;
    if (price <= 0 || price < ctx.s.vp.pMin || price > ctx.s.vp.pMax) return false;

    const qreal x = static_cast<qreal>(std::round(ctx.s.vp.toX(sample.tsNs)));
    const qreal y = static_cast<qreal>(std::round(ctx.s.vp.toY(price)));
    if (x < 0.0 || x > ctx.s.vp.w || y < 0.0 || y > ctx.s.vp.h) return false;

    out = QPointF{x, y};
    return true;
}

void drawSampleSide(const RenderContext& ctx,
                    const std::vector<BookTickerSample>& samples,
                    bool bidSide,
                    const QPen& pen) {
    QPointF prev{};
    bool hasPrev = false;

    ctx.p->setPen(pen);
    for (const auto& sample : samples) {
        QPointF point{};
        if (!sampleSidePoint(ctx, sample, bidSide, point)) {
            hasPrev = false;
            continue;
        }

        if (hasPrev) {
            const QPointF corner{point.x(), prev.y()};
            if (corner != prev) ctx.p->drawLine(prev, corner);
            if (point != corner) ctx.p->drawLine(corner, point);
        }

        prev = point;
        hasPrev = true;
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
    drawSampleSide(ctx, ctx.s.bookTickerTrace.samples, true, bidPen);
    drawSampleSide(ctx, ctx.s.bookTickerTrace.samples, false, askPen);
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers