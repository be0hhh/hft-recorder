#include "gui/viewer/ChartItem.hpp"

#include <memory>

#include <QImage>
#include <QPainter>
#include <QQuickWindow>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/ChartItemPaintInternal.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"

namespace hftrec::gui::viewer {

namespace {

void paintSnapshotFrame(QPainter* painter,
                        const RenderSnapshot& snap,
                        const HoverInfo& hover,
                        double dpr,
                        bool drawBackground) {
    const QRectF rect{0.0, 0.0, snap.vp.w, snap.vp.h};
    if (drawBackground && !snap.overlayOnly) painter->fillRect(rect, bgColor());

    if (!snap.loaded) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, snap.vp.w - 16, 24},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Pick a session, then load Trades."));
        return;
    }
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return;

    RenderContext ctx{painter, snap, hover, dpr};
    renderers::renderBook(ctx);
    renderers::renderTrades(ctx);
    renderers::renderOverlay(ctx);
}

void paintSnapshotLayers(QPainter* painter,
                         const RenderSnapshot& snap,
                         bool drawBackground,
                         bool drawOrderbook,
                         bool drawTrades,
                         bool drawOverlay,
                         const HoverInfo& hover,
                         double dpr) {
    RenderSnapshot layerSnap = snap;
    layerSnap.orderbookVisible = drawOrderbook && snap.orderbookVisible;
    layerSnap.bookTickerVisible = false;
    layerSnap.tradesVisible = drawTrades && snap.tradesVisible;
    if (drawBackground && !layerSnap.overlayOnly) {
        painter->fillRect(QRectF{0.0, 0.0, layerSnap.vp.w, layerSnap.vp.h}, bgColor());
    }
    if (!layerSnap.loaded) return;
    if (layerSnap.vp.tMax <= layerSnap.vp.tMin || layerSnap.vp.pMax <= layerSnap.vp.pMin) return;

    RenderContext ctx{painter, layerSnap, drawOverlay ? hover : HoverInfo{}, dpr};
    if (layerSnap.orderbookVisible) renderers::renderBook(ctx);
    if (layerSnap.tradesVisible) renderers::renderTrades(ctx);
    if (drawOverlay) renderers::renderOverlay(ctx);
}

QRectF sourceRectForViewport(const ViewportMap& cachedVp,
                             qint64 tsMin,
                             qint64 tsMax,
                             qint64 priceMinE8,
                             qint64 priceMaxE8) {
    const qreal x0 = cachedVp.toX(tsMin);
    const qreal x1 = cachedVp.toX(tsMax);
    const qreal y0 = cachedVp.toY(priceMaxE8);
    const qreal y1 = cachedVp.toY(priceMinE8);
    return QRectF{
        std::min(x0, x1),
        std::min(y0, y1),
        std::abs(x1 - x0),
        std::abs(y1 - y0),
    };
}

}  // namespace

void ChartItem::requestRepaint() {
    if (interactiveMode_ && cachedExactSnap_) {
        snapshotDirty_ = true;
    } else {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        snapshotDirty_ = false;
    }
    if (hoverActive_ && !interactiveMode_) updateHover_();
    update();
}

void ChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        snapshotDirty_ = false;
    }
}

void ChartItem::invalidateSnapshotCache_() {
    cachedInteractiveSnap_.reset();
    cachedExactSnap_.reset();
}

void ChartItem::invalidateBaseImage_() {
    cachedOrderbookImage_ = QImage{};
    cachedTradesImage_ = QImage{};
    cachedLayerImageW_ = 0.0;
    cachedLayerImageH_ = 0.0;
}

std::unique_ptr<RenderSnapshot>& ChartItem::activeSnapshotCache_() noexcept {
    return interactiveMode_ ? cachedInteractiveSnap_ : cachedExactSnap_;
}

const RenderSnapshot& ChartItem::ensureSnapshot_() {
    const qreal w = width();
    const qreal h = height();
    auto& activeCache = activeSnapshotCache_();
    const bool sizeChanged = (cachedW_ != w || cachedH_ != h);

    if (interactiveMode_ && !sizeChanged && cachedExactSnap_) {
        return *cachedExactSnap_;
    }

    if (!activeCache || sizeChanged) {
        activeCache = std::make_unique<RenderSnapshot>(controller_->buildSnapshot(w, h, detail::collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
        snapshotDirty_ = false;
    }
    return *activeCache;
}

void ChartItem::ensureLayerImages_(const RenderSnapshot& snap, qreal w, qreal h) {
    if (overlayOnly_) return;
    if (!snap.loaded) return;
    if (!cachedOrderbookImage_.isNull() && !cachedTradesImage_.isNull()
        && cachedLayerImageW_ == w && cachedLayerImageH_ == h) return;

    const int imageW = std::max(1, static_cast<int>(std::ceil(w)));
    const int imageH = std::max(1, static_cast<int>(std::ceil(h)));
    QImage orderbookImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
    orderbookImage.fill(bgColor().rgba());
    QImage tradesImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
    tradesImage.fill(Qt::transparent);

    {
        QPainter painter(&orderbookImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, snap, false, true, false, false, HoverInfo{}, 1.0);
        painter.end();
    }
    {
        QPainter painter(&tradesImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, snap, false, false, true, false, HoverInfo{}, 1.0);
        painter.end();
    }

    cachedOrderbookImage_ = std::move(orderbookImage);
    cachedTradesImage_ = std::move(tradesImage);
    cachedLayerImageW_ = w;
    cachedLayerImageH_ = h;
}

void ChartItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF rect = boundingRect();
    if (!overlayOnly_) painter->fillRect(rect, bgColor());

    if (!controller_ || width() <= 0 || height() <= 0) return;

    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal w = width();
    const qreal h = height();

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) {
        paintSnapshotFrame(painter, snap, detail::buildHoverInfo(*this), dpr, false);
        return;
    }

    ensureLayerImages_(snap, w, h);

    if (interactiveMode_ && !cachedOrderbookImage_.isNull() && !cachedTradesImage_.isNull() && cachedExactSnap_) {
        const QRectF sourceRect = sourceRectForViewport(
            cachedExactSnap_->vp,
            controller_->tsMin(),
            controller_->tsMax(),
            controller_->priceMinE8(),
            controller_->priceMaxE8());
        const QRectF fullSource{
            0.0,
            0.0,
            static_cast<qreal>(cachedOrderbookImage_.width()),
            static_cast<qreal>(cachedOrderbookImage_.height())
        };
        const QRectF clippedSource = sourceRect.intersected(fullSource);
        if (sourceRect.width() > 0.5 && sourceRect.height() > 0.5
            && clippedSource.width() > 0.5 && clippedSource.height() > 0.5) {
            const QRectF destRect{
                rect.left() + ((clippedSource.left() - sourceRect.left()) / sourceRect.width()) * rect.width(),
                rect.top() + ((clippedSource.top() - sourceRect.top()) / sourceRect.height()) * rect.height(),
                (clippedSource.width() / sourceRect.width()) * rect.width(),
                (clippedSource.height() / sourceRect.height()) * rect.height(),
            };
            painter->drawImage(destRect, cachedOrderbookImage_, clippedSource);
            detail::paintBookTickerLayer(painter, *controller_, *this, *cachedExactSnap_, w, h, dpr, true);
            painter->drawImage(destRect, cachedTradesImage_, clippedSource);
            return;
        }
    }

    if (!cachedOrderbookImage_.isNull() && !overlayOnly_) {
        painter->drawImage(rect, cachedOrderbookImage_);
    }
    detail::paintBookTickerLayer(painter, *controller_, *this, snap, w, h, dpr, false);
    if (!cachedTradesImage_.isNull()) {
        painter->drawImage(rect, cachedTradesImage_);
    }

    if (!interactiveMode_) {
        RenderContext ctx{painter, snap, detail::buildHoverInfo(*this), dpr};
        renderers::renderOverlay(ctx);
    }
}

}  // namespace hftrec::gui::viewer
