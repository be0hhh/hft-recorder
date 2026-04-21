#include "gui/viewer/renderers/BookTickerRenderer.hpp"

#include <algorithm>
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

void drawTraceLines(QPainter* painter,
                    const std::vector<BookTickerLine>& lines,
                    const QPen& pen) {
    if (lines.empty()) return;
    painter->setPen(pen);
    for (const auto& line : lines) {
        painter->drawLine(QPointF{line.x0, line.y0}, QPointF{line.x1, line.y1});
    }
}

}  // namespace

void renderBookTicker(const RenderContext& ctx) {
    if (!ctx.s.bookTickerVisible) return;
    if (ctx.s.bookTickerTrace.bidLines.empty() && ctx.s.bookTickerTrace.askLines.empty()) return;

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
    drawTraceLines(ctx.p, ctx.s.bookTickerTrace.bidLines, bidPen);
    drawTraceLines(ctx.p, ctx.s.bookTickerTrace.askLines, askPen);
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
