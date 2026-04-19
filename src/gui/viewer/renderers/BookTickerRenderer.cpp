#include "gui/viewer/renderers/BookTickerRenderer.hpp"

#include <algorithm>

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer::renderers {

void renderBookTicker(const RenderContext& ctx) {
    if (!ctx.s.bookTickerVisible || ctx.s.bookSegments.empty()) return;

    QPainterPath bidPath;
    QPainterPath askPath;
    bool bidStarted = false;
    bool askStarted = false;

    for (const auto& seg : ctx.s.bookSegments) {
        const qreal xLeft  = std::clamp(ctx.s.vp.toX(seg.tsStartNs), 0.0, ctx.s.vp.w);
        const qreal xRight = std::clamp(ctx.s.vp.toX(seg.tsEndNs),   0.0, ctx.s.vp.w);
        if (xRight <= xLeft) continue;
        if (seg.tickerBidE8 != 0) {
            detail::appendStepSegment(bidPath, bidStarted, xLeft, xRight,
                                      ctx.s.vp.toY(seg.tickerBidE8));
        }
        if (seg.tickerAskE8 != 0) {
            detail::appendStepSegment(askPath, askStarted, xLeft, xRight,
                                      ctx.s.vp.toY(seg.tickerAskE8));
        }
    }

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
    ctx.p->setPen(bidPen);
    ctx.p->drawPath(bidPath);
    ctx.p->setPen(askPen);
    ctx.p->drawPath(askPath);
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
