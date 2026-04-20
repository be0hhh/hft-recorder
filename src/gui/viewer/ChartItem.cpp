#include "gui/viewer/ChartItem.hpp"

#include <algorithm>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer {

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::Image);
    setOpaquePainting(true);
}

ChartItem::~ChartItem() = default;

void ChartItem::setController(ChartController* c) {
    if (controller_ == c) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged, this, &ChartItem::requestRepaint);
        connect(controller_, &ChartController::sessionChanged, this, &ChartItem::requestRepaint);
    }
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    emit controllerChanged();
    update();
}

void ChartItem::setTradesVisible(bool value) {
    if (tradesVisible_ == value) return;
    tradesVisible_ = value;
    if (!tradesVisible_) clearHover();
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    emit tradesVisibleChanged();
    update();
}

void ChartItem::setOrderbookVisible(bool value) {
    if (orderbookVisible_ == value) return;
    orderbookVisible_ = value;
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    updateHover_();
    emit orderbookVisibleChanged();
    update();
}

void ChartItem::setBookTickerVisible(bool value) {
    if (bookTickerVisible_ == value) return;
    bookTickerVisible_ = value;
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    updateHover_();
    emit bookTickerVisibleChanged();
    update();
}

void ChartItem::setTradeAmountScale(qreal value) {
    value = detail::clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    emit tradeAmountScaleChanged();
    update();
}

void ChartItem::setBookOpacityGain(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookOpacityGain_ + 1.0, value + 1.0)) return;
    bookOpacityGain_ = value;
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    emit bookOpacityGainChanged();
    update();
}

void ChartItem::setBookRenderDetail(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookRenderDetail_ + 1.0, value + 1.0)) return;
    bookRenderDetail_ = value;
    invalidateSnapshotCache_();
    invalidateBaseImage_();
    emit bookRenderDetailChanged();
    update();
}

void ChartItem::setInteractiveMode(bool value) {
    if (interactiveMode_ == value) return;
    interactiveMode_ = value;
    if (!interactiveMode_ && snapshotDirty_) {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        snapshotDirty_ = false;
    }
    emit interactiveModeChanged();
    update();
}

void ChartItem::setOverlayOnly(bool value) {
    if (overlayOnly_ == value) return;
    overlayOnly_ = value;
    setOpaquePainting(!overlayOnly_);
    emit overlayOnlyChanged();
    update();
}

}  // namespace hftrec::gui::viewer

namespace hftrec::gui::viewer::detail {

SnapshotInputs collectInputs(const ChartItem& item) {
    return SnapshotInputs{
        item.tradesVisible(),
        item.orderbookVisible(),
        item.bookTickerVisible(),
        item.interactiveMode(),
        item.overlayOnly(),
        !item.interactiveMode(),
        item.tradeAmountScale(),
        item.bookOpacityGain(),
        item.bookRenderDetail(),
    };
}

}  // namespace hftrec::gui::viewer::detail
