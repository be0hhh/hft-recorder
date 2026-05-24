#include "gui/viewer/renderers/CandleRenderer.hpp"

#include <algorithm>

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"

namespace hftrec::gui::viewer::renderers {

void renderCandles(const RenderContext& ctx) {
    if (!ctx.s.candlesVisible || ctx.s.candleRects.empty()) return;

    ctx.p->save();
    ctx.p->setRenderHint(QPainter::Antialiasing, false);
    for (const auto& candle : ctx.s.candleRects) {
        QRectF body{candle.x, candle.y, candle.w, candle.h};
        if (body.right() < 0.0 || body.left() > ctx.s.vp.w || body.bottom() < 0.0 || body.top() > ctx.s.vp.h) continue;
        QColor fill = candle.up ? tradeBuyColor() : tradeSellColor();
        const int tierBoost = candle.tier == 1 ? 0 : (candle.tier == 2 ? 24 : 44);
        fill.setAlpha(std::clamp(92 + tierBoost, 60, 170));
        QColor stroke = fill;
        stroke.setAlpha(std::clamp(fill.alpha() + 48, 90, 210));
        QPen pen(stroke);
        pen.setWidth(candle.tier == 3 ? 2 : 1);
        pen.setCapStyle(Qt::SquareCap);
        ctx.p->setPen(pen);
        ctx.p->setBrush(QBrush(fill));
        ctx.p->drawRect(body);
    }
    ctx.p->restore();
}

}  // namespace hftrec::gui::viewer::renderers
