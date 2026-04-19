#include "gui/viewer/ChartItem.hpp"

#include <memory>

#include <QPainter>
#include <QQuickWindow>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"

namespace hftrec::gui::viewer {

void ChartItem::requestRepaint() {
    invalidateSnapshotCache_();
    updateHover_();
    update();
}

void ChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
    }
}

void ChartItem::invalidateSnapshotCache_() {
    cachedSnap_.reset();
}

const RenderSnapshot& ChartItem::ensureSnapshot_() {
    const qreal w = width();
    const qreal h = height();
    if (!cachedSnap_ || cachedW_ != w || cachedH_ != h) {
        cachedSnap_ = std::make_unique<RenderSnapshot>(controller_->buildSnapshot(w, h, detail::collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
    }
    return *cachedSnap_;
}

void ChartItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF rect = boundingRect();
    if (!overlayOnly_) painter->fillRect(rect, bgColor());

    if (!controller_ || width() <= 0 || height() <= 0) return;

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, snap.vp.w - 16, 24},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Pick a session, then load Trades."));
        return;
    }
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return;

    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    RenderContext ctx{painter, snap, detail::buildHoverInfo(*this), dpr};

    renderers::renderBook(ctx);
    renderers::renderBookTicker(ctx);
    renderers::renderTrades(ctx);
    renderers::renderOverlay(ctx);
}

}  // namespace hftrec::gui::viewer
