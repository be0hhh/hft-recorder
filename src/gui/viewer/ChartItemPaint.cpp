#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QQuickWindow>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/ChartItemPaintInternal.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
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
    layerSnap.tradeConnectorsVisible = drawTrades && snap.tradeConnectorsVisible;
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

RenderSnapshot baseSnapshotForCache(const RenderSnapshot& snap) {
    RenderSnapshot base = snap;
    if (!base.tradeDots.empty()) base.tradeDots.pop_back();
    return base;
}

RenderSnapshot liveSnapshotForLastEvent(const RenderSnapshot& snap) {
    RenderSnapshot live = snap;
    if (live.tradeDots.size() > 1u) {
        const TradeDot last = live.tradeDots.back();
        live.tradeDots.clear();
        live.tradeDots.push_back(last);
    }
    live.tradeConnectorsVisible = false;
    live.orderbookVisible = false;
    live.bookTickerVisible = false;
    live.bookSegments.clear();
    live.bookTickerTrace = BookTickerTrace{};
    live.gpuBookVertices.clear();
    return live;
}

void drawTradeBridge(QPainter* painter, const RenderSnapshot& base, const RenderSnapshot& live) {
    if (!base.tradesVisible || base.tradeDots.empty() || live.tradeDots.empty()) return;
    const auto& prev = base.tradeDots.back();
    const auto& last = live.tradeDots.front();
    if (prev.origIndex + 1 != last.origIndex) return;

    const QPointF p0{base.vp.toX(prev.tsNs), base.vp.toY(prev.priceE8)};
    const QPointF p1{base.vp.toX(last.tsNs), base.vp.toY(last.priceE8)};
    if (p0 == p1) return;

    painter->save();
    QPen pen(tradeConnectorColor());
    pen.setWidth(1);
    pen.setCapStyle(Qt::SquareCap);
    painter->setPen(pen);
    painter->drawLine(p0, p1);
    painter->restore();
}

void paintLiveSnapshot(QPainter* painter,
                       const RenderSnapshot& base,
                       const RenderSnapshot& live,
                       double dpr) {
    if (!live.loaded) return;
    RenderContext ctx{painter, live, HoverInfo{}, dpr};
    drawTradeBridge(painter, base, live);
    renderers::renderTrades(ctx);
}

}  // namespace

void ChartItem::requestRepaint() {
    if (interactiveMode_ && cachedExactSnap_) {
        cachedInteractiveSnap_.reset();
        interactiveDirty_ = true;
    } else {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
    if (hoverActive_ && !interactiveMode_) updateHover_();
    update();
}

void ChartItem::requestLiveRepaint() {
    mergeLiveSnapshotIntoBaseImage_();
    cachedOrderbookImage_ = QImage{};
    cachedInteractiveSnap_.reset();
    cachedExactSnap_.reset();
    interactiveDirty_ = true;
    exactDirty_ = true;
    if (hoverActive_ && !interactiveMode_) updateHover_();
    update();
}

void ChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
}

void ChartItem::invalidateSnapshotCache_() {
    cachedInteractiveSnap_.reset();
    cachedExactSnap_.reset();
    interactiveDirty_ = false;
    exactDirty_ = false;
}

void ChartItem::invalidateBaseImage_() {
    cachedOrderbookImage_ = QImage{};
    cachedTradesImage_ = QImage{};
    cachedLayerImageW_ = 0.0;
    cachedLayerImageH_ = 0.0;
    cachedLiveSnap_.reset();
}

std::unique_ptr<RenderSnapshot>& ChartItem::activeSnapshotCache_() noexcept {
    return interactiveMode_ ? cachedInteractiveSnap_ : cachedExactSnap_;
}

void ChartItem::mergeLiveSnapshotIntoBaseImage_() {
    if (!cachedLiveSnap_ || cachedLiveSnap_->overlayOnly) return;
    if (cachedLayerImageW_ <= 0.0 || cachedLayerImageH_ <= 0.0) return;

    if (!cachedTradesImage_.isNull()) {
        QPainter painter(&cachedTradesImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveTrades = *cachedLiveSnap_;
        liveTrades.orderbookVisible = false;
        liveTrades.bookTickerVisible = false;
        const RenderSnapshot base = cachedExactSnap_ ? baseSnapshotForCache(*cachedExactSnap_) : RenderSnapshot{};
        drawTradeBridge(&painter, base, liveTrades);
        RenderContext ctx{&painter, liveTrades, HoverInfo{}, 1.0};
        renderers::renderTrades(ctx);
        painter.end();
    }
}

const RenderSnapshot& ChartItem::ensureSnapshot_() {
    const qreal w = width();
    const qreal h = height();
    auto& activeCache = activeSnapshotCache_();
    bool& activeDirty = interactiveMode_ ? interactiveDirty_ : exactDirty_;
    const bool sizeChanged = (cachedW_ != w || cachedH_ != h);
    const bool rebuildActive = activeDirty || !activeCache || sizeChanged;

    if (rebuildActive) {
        activeCache = std::make_unique<RenderSnapshot>(controller_->buildSnapshot(w, h, detail::collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
        activeDirty = false;
    }
    return *activeCache;
}

void ChartItem::ensureLayerImages_(const RenderSnapshot& snap, qreal w, qreal h) {
    if (overlayOnly_) return;
    if (!snap.loaded) return;
    const bool sizeMatches = (cachedLayerImageW_ == w && cachedLayerImageH_ == h);
    if (!sizeMatches) {
        cachedOrderbookImage_ = QImage{};
        cachedTradesImage_ = QImage{};
    }
    if (!cachedOrderbookImage_.isNull() && !cachedTradesImage_.isNull()) return;

    const RenderSnapshot baseSnap = baseSnapshotForCache(snap);

    const int imageW = std::max(1, static_cast<int>(std::ceil(w)));
    const int imageH = std::max(1, static_cast<int>(std::ceil(h)));

    if (cachedOrderbookImage_.isNull()) {
        QImage orderbookImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        orderbookImage.fill(bgColor().rgba());
        QPainter painter(&orderbookImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, true, false, false, HoverInfo{}, 1.0);
        painter.end();
        cachedOrderbookImage_ = std::move(orderbookImage);
    }

    if (cachedTradesImage_.isNull()) {
        QImage tradesImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        tradesImage.fill(Qt::transparent);
        QPainter painter(&tradesImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot cachedTradesSnap = baseSnap;
        cachedTradesSnap.tradeConnectorsVisible = true;
        paintSnapshotLayers(&painter, cachedTradesSnap, false, false, true, false, HoverInfo{}, 1.0);
        painter.end();
        cachedTradesImage_ = std::move(tradesImage);
    }

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

    const RenderSnapshot& layerSnap = (interactiveMode_ && cachedExactSnap_) ? *cachedExactSnap_ : snap;
    ensureLayerImages_(layerSnap, w, h);
    const RenderSnapshot baseSnap = baseSnapshotForCache(snap);
    cachedLiveSnap_ = std::make_unique<RenderSnapshot>(liveSnapshotForLastEvent(snap));

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
            paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);
            return;
        }
    }

    if (!cachedOrderbookImage_.isNull() && !overlayOnly_) {
        painter->drawImage(rect, cachedOrderbookImage_);
    }
    detail::paintBookTickerLayer(painter, *controller_, *this, baseSnap, w, h, dpr, false);
    if (!cachedTradesImage_.isNull()) {
        painter->drawImage(rect, cachedTradesImage_);
    }
    paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);

    if (!interactiveMode_) {
        RenderContext ctx{painter, snap, detail::buildHoverInfo(*this), dpr};
        renderers::renderOverlay(ctx);
    }
}

}  // namespace hftrec::gui::viewer
