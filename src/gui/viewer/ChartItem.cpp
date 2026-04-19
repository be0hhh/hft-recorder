#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <memory>

#include <QPainter>
#include <QQuickWindow>
#include <QRectF>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/Formatters.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"

namespace hftrec::gui::viewer {

namespace {

SnapshotInputs collectInputs(const ChartItem& item) {
    return SnapshotInputs{
        item.tradesVisible(),
        item.orderbookVisible(),
        item.bookTickerVisible(),
        item.interactiveMode(),
        item.overlayOnly(),
        item.tradeAmountScale(),
        item.bookOpacityGain(),
        item.bookRenderDetail(),
    };
}

}  // namespace

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAntialiasing(false);
    // Image target: QPainter rasterizes into a CPU QImage, scene graph
    // uploads it as a texture once per paint. Simple and works on every
    // Qt 6 backend (including the software RHI).
    setRenderTarget(QQuickPaintedItem::Image);
}

ChartItem::~ChartItem() = default;

void ChartItem::setController(ChartController* c) {
    if (controller_ == c) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged,
                this, &ChartItem::requestRepaint);
        connect(controller_, &ChartController::sessionChanged,
                this, &ChartItem::requestRepaint);
    }
    invalidateSnapshotCache_();
    emit controllerChanged();
    update();
}

void ChartItem::setTradesVisible(bool value) {
    if (tradesVisible_ == value) return;
    tradesVisible_ = value;
    if (!tradesVisible_) clearHover();
    invalidateSnapshotCache_();
    emit tradesVisibleChanged();
    update();
}

void ChartItem::setOrderbookVisible(bool value) {
    if (orderbookVisible_ == value) return;
    orderbookVisible_ = value;
    invalidateSnapshotCache_();
    updateHover_();
    emit orderbookVisibleChanged();
    update();
}

void ChartItem::setBookTickerVisible(bool value) {
    if (bookTickerVisible_ == value) return;
    bookTickerVisible_ = value;
    invalidateSnapshotCache_();
    updateHover_();
    emit bookTickerVisibleChanged();
    update();
}

void ChartItem::setTradeAmountScale(qreal value) {
    value = detail::clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    invalidateSnapshotCache_();
    emit tradeAmountScaleChanged();
    update();
}

void ChartItem::setBookOpacityGain(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookOpacityGain_ + 1.0, value + 1.0)) return;
    bookOpacityGain_ = value;
    invalidateSnapshotCache_();
    emit bookOpacityGainChanged();
    update();
}

void ChartItem::setBookRenderDetail(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookRenderDetail_ + 1.0, value + 1.0)) return;
    bookRenderDetail_ = value;
    invalidateSnapshotCache_();
    emit bookRenderDetailChanged();
    update();
}

void ChartItem::setInteractiveMode(bool value) {
    if (interactiveMode_ == value) return;
    interactiveMode_ = value;
    invalidateSnapshotCache_();
    emit interactiveModeChanged();
    update();
}

void ChartItem::setOverlayOnly(bool value) {
    if (overlayOnly_ == value) return;
    overlayOnly_ = value;
    emit overlayOnlyChanged();
    update();
}

void ChartItem::setHoverPoint(qreal x, qreal y) {
    hoverPoint_     = QPointF{x, y};
    hoverActive_    = true;
    contextActive_  = false;
    updateHover_();
    update();
}

void ChartItem::activateContextPoint(qreal x, qreal y) {
    hoverPoint_     = QPointF{x, y};
    hoverActive_    = true;
    contextActive_  = true;
    updateHover_();
    update();
}

void ChartItem::clearHover() {
    hoverActive_        = false;
    contextActive_      = false;
    hoveredTradeIndex_  = -1;
    hoveredBookKind_    = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_   = 0;
    hoveredBookTsNs_    = 0;
    update();
}

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
        cachedSnap_ = std::make_unique<RenderSnapshot>(
            controller_->buildSnapshot(w, h, collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
    }
    return *cachedSnap_;
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_  = -1;
    hoveredBookKind_    = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_   = 0;
    hoveredBookTsNs_    = 0;
    if (!hoverActive_ || !controller_ || !controller_->loaded() ||
        width() <= 0 || height() <= 0) return;

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) return;

    HoverInfo hov{};
    hit_test::computeHover(snap, hoverPoint_, contextActive_, hov);

    hoveredTradeIndex_  = hov.tradeHit ? hov.tradeOrigIndex : -1;
    hoveredBookKind_    = hov.bookKind;
    hoveredBookPriceE8_ = hov.bookPriceE8;
    hoveredBookQtyE8_   = hov.bookQtyE8;
    hoveredBookTsNs_    = hov.bookTsNs;
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

    HoverInfo hov{};
    hov.active        = hoverActive_;
    hov.contextActive = contextActive_;
    hov.point         = hoverPoint_;
    hov.bookKind      = hoveredBookKind_;
    hov.bookPriceE8   = hoveredBookPriceE8_;
    hov.bookQtyE8     = hoveredBookQtyE8_;
    hov.bookTsNs      = hoveredBookTsNs_;
    if (hoveredTradeIndex_ >= 0 && controller_) {
        const auto& trades = controller_->replay().trades();
        if (hoveredTradeIndex_ < static_cast<int>(trades.size())) {
            const auto& t = trades[static_cast<std::size_t>(hoveredTradeIndex_)];
            hov.tradeHit       = true;
            hov.tradeOrigIndex = hoveredTradeIndex_;
            hov.tradeTsNs      = t.tsNs;
            hov.tradePriceE8   = t.priceE8;
            hov.tradeQtyE8     = t.qtyE8;
            hov.tradeSideBuy   = t.sideBuy;
        }
    }

    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    RenderContext ctx{painter, snap, hov, dpr};

    renderers::renderBook(ctx);
    renderers::renderBookTicker(ctx);
    renderers::renderTrades(ctx);
    renderers::renderOverlay(ctx);
}

}  // namespace hftrec::gui::viewer
