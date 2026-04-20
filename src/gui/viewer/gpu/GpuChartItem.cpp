#include "gui/viewer/gpu/GpuChartItem.hpp"

#include <algorithm>
#include <memory>

#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QQuickWindow>

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

namespace hftrec::gui::viewer::gpu {

namespace {

SnapshotInputs collectInputs(const GpuChartItem& item) {
    return SnapshotInputs{
        item.tradesVisible(),
        item.orderbookVisible(),
        item.bookTickerVisible(),
        item.interactiveMode(),
        item.overlayOnly(),
        true,
        item.tradeAmountScale(),
        item.bookOpacityGain(),
        item.bookRenderDetail(),
    };
}

}  // namespace

class GpuChartRenderer final : public QQuickFramebufferObject::Renderer {
  public:
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        const auto* gpuItem = static_cast<GpuChartItem*>(item);
        snapshot_ = gpuItem->snapshotCopy_();
        hover_ = gpuItem->hoverInfoCopy_();
        dpr_ = gpuItem->window() ? gpuItem->window()->effectiveDevicePixelRatio() : 1.0;
    }

    void render() override {
        auto* fbo = framebufferObject();
        if (fbo == nullptr) return;

        QOpenGLPaintDevice device(fbo->size());
        QPainter painter(&device);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        if (dpr_ > 0.0) painter.scale(dpr_, dpr_);

        const QRectF rect{0.0, 0.0, snapshot_.vp.w, snapshot_.vp.h};
        if (!snapshot_.overlayOnly) painter.fillRect(rect, bgColor());

        if (!snapshot_.loaded) {
            painter.setPen(axisTextColor());
            painter.drawText(QRectF{8, 8, snapshot_.vp.w - 16, 24},
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("Pick a session, then load Trades."));
        } else if (snapshot_.vp.tMax > snapshot_.vp.tMin && snapshot_.vp.pMax > snapshot_.vp.pMin) {
            RenderContext ctx{&painter, snapshot_, hover_, dpr_};
            renderers::renderBook(ctx);
            renderers::renderBookTicker(ctx);
            renderers::renderTrades(ctx);
            renderers::renderOverlay(ctx);
        }

        painter.end();
    }

  private:
    RenderSnapshot snapshot_{};
    HoverInfo hover_{};
    double dpr_{1.0};
};

GpuChartItem::GpuChartItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    setFlag(ItemHasContents, true);
}

GpuChartItem::~GpuChartItem() = default;

void GpuChartItem::setController(ChartController* c) {
    if (controller_ == c) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged, this, &GpuChartItem::requestRepaint);
        connect(controller_, &ChartController::sessionChanged, this, &GpuChartItem::requestRepaint);
    }
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit controllerChanged();
    update();
}

void GpuChartItem::setTradesVisible(bool value) {
    if (tradesVisible_ == value) return;
    tradesVisible_ = value;
    if (!tradesVisible_) clearHover();
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit tradesVisibleChanged();
    update();
}

void GpuChartItem::setOrderbookVisible(bool value) {
    if (orderbookVisible_ == value) return;
    orderbookVisible_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    updateHover_();
    emit orderbookVisibleChanged();
    update();
}

void GpuChartItem::setBookTickerVisible(bool value) {
    if (bookTickerVisible_ == value) return;
    bookTickerVisible_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    updateHover_();
    emit bookTickerVisibleChanged();
    update();
}

void GpuChartItem::setTradeAmountScale(qreal value) {
    value = detail::clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit tradeAmountScaleChanged();
    update();
}

void GpuChartItem::setBookOpacityGain(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookOpacityGain_ + 1.0, value + 1.0)) return;
    bookOpacityGain_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit bookOpacityGainChanged();
    update();
}

void GpuChartItem::setBookRenderDetail(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookRenderDetail_ + 1.0, value + 1.0)) return;
    bookRenderDetail_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit bookRenderDetailChanged();
    update();
}

void GpuChartItem::setInteractiveMode(bool value) {
    if (interactiveMode_ == value) return;
    interactiveMode_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit interactiveModeChanged();
    update();
}

void GpuChartItem::setOverlayOnly(bool value) {
    if (overlayOnly_ == value) return;
    overlayOnly_ = value;
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    emit overlayOnlyChanged();
    update();
}

QQuickFramebufferObject::Renderer* GpuChartItem::createRenderer() const {
    return new GpuChartRenderer{};
}

bool GpuChartItem::shouldSkipHoverRecompute_(const QPointF& point, bool contextActive) const noexcept {
    if (!hoverActive_ || contextActive_ != contextActive) return false;
    const qreal dx = point.x() - hoverPoint_.x();
    const qreal dy = point.y() - hoverPoint_.y();
    return (dx * dx + dy * dy) < 0.25;
}

void GpuChartItem::setHoverPoint(qreal x, qreal y) {
    if (shouldSkipHoverRecompute_(QPointF{x, y}, false)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = false;
    updateHover_();
    update();
}

void GpuChartItem::activateContextPoint(qreal x, qreal y) {
    if (shouldSkipHoverRecompute_(QPointF{x, y}, true)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = true;
    updateHover_();
    update();
}

void GpuChartItem::clearHover() {
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoverInfo_ = std::make_unique<HoverInfo>();
    update();
}

void GpuChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoverInfo_ = std::make_unique<HoverInfo>();
    if (!hoverActive_ || !controller_ || !controller_->loaded() || width() <= 0 || height() <= 0) return;

    rebuildSnapshot_();
    if (!cachedSnap_ || !cachedSnap_->loaded) return;

    HoverInfo hover{};
    hit_test::computeHover(*cachedSnap_, hoverPoint_, contextActive_, hover);
    hoveredTradeIndex_ = hover.tradeHit ? hover.tradeOrigIndex : -1;
    hoveredBookKind_ = hover.bookKind;
    hoveredBookPriceE8_ = hover.bookPriceE8;
    hoveredBookQtyE8_ = hover.bookQtyE8;
    hoveredBookTsNs_ = hover.bookTsNs;
    *hoverInfo_ = buildHoverInfo_();
}

void GpuChartItem::requestRepaint() {
    invalidateSnapshotCache_();
    rebuildSnapshot_();
    updateHover_();
    update();
}

void GpuChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        rebuildSnapshot_();
    }
}

void GpuChartItem::invalidateSnapshotCache_() {
    cachedSnap_.reset();
}

void GpuChartItem::rebuildSnapshot_() {
    if (!controller_ || width() <= 0.0 || height() <= 0.0) return;
    const qreal w = width();
    const qreal h = height();
    if (!cachedSnap_ || cachedW_ != w || cachedH_ != h) {
        cachedSnap_ = std::make_unique<RenderSnapshot>(
            controller_->buildSnapshot(w, h, collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
    }
    if (!hoverInfo_) hoverInfo_ = std::make_unique<HoverInfo>(buildHoverInfo_());
}

HoverInfo GpuChartItem::buildHoverInfo_() const {
    HoverInfo hover{};
    hover.active = hoverActive_;
    hover.contextActive = contextActive_;
    hover.point = hoverPoint_;
    hover.bookKind = hoveredBookKind_;
    hover.bookPriceE8 = hoveredBookPriceE8_;
    hover.bookQtyE8 = hoveredBookQtyE8_;
    hover.bookTsNs = hoveredBookTsNs_;

    if (hoveredTradeIndex_ < 0 || controller_ == nullptr) {
        return hover;
    }

    const auto& trades = controller_->replay().trades();
    if (hoveredTradeIndex_ >= static_cast<int>(trades.size())) {
        return hover;
    }

    const auto& trade = trades[static_cast<std::size_t>(hoveredTradeIndex_)];
    hover.tradeHit = true;
    hover.tradeOrigIndex = hoveredTradeIndex_;
    hover.tradeTsNs = trade.tsNs;
    hover.tradePriceE8 = trade.priceE8;
    hover.tradeQtyE8 = trade.qtyE8;
    hover.tradeSideBuy = trade.sideBuy;
    return hover;
}

RenderSnapshot GpuChartItem::snapshotCopy_() const {
    return cachedSnap_ ? *cachedSnap_ : RenderSnapshot{};
}

HoverInfo GpuChartItem::hoverInfoCopy_() const {
    return hoverInfo_ ? *hoverInfo_ : HoverInfo{};
}

}  // namespace hftrec::gui::viewer::gpu
