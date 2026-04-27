#include "gui/viewer/ChartItem.hpp"

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace hftrec::gui::viewer {

namespace {

RenderSnapshot hoverSnapshotFrom(const RenderSnapshot& snap, const RenderSnapshot* liveSnap) {
    RenderSnapshot merged = snap;
    if (liveSnap == nullptr || !liveSnap->loaded) return merged;

    merged.bookSegments.insert(merged.bookSegments.end(), liveSnap->bookSegments.begin(), liveSnap->bookSegments.end());
    merged.bookTickerTrace.bidLines.insert(
        merged.bookTickerTrace.bidLines.end(),
        liveSnap->bookTickerTrace.bidLines.begin(),
        liveSnap->bookTickerTrace.bidLines.end());
    merged.bookTickerTrace.askLines.insert(
        merged.bookTickerTrace.askLines.end(),
        liveSnap->bookTickerTrace.askLines.begin(),
        liveSnap->bookTickerTrace.askLines.end());
    merged.bookTickerTrace.samples.insert(
        merged.bookTickerTrace.samples.end(),
        liveSnap->bookTickerTrace.samples.begin(),
        liveSnap->bookTickerTrace.samples.end());
    merged.tradeDots.insert(merged.tradeDots.end(), liveSnap->tradeDots.begin(), liveSnap->tradeDots.end());
    merged.liquidationDots.insert(merged.liquidationDots.end(), liveSnap->liquidationDots.begin(), liveSnap->liquidationDots.end());
    return merged;
}

bool sameViewport(const RenderSnapshot& lhs, const RenderSnapshot& rhs) noexcept {
    return lhs.vp.tMin == rhs.vp.tMin
        && lhs.vp.tMax == rhs.vp.tMax
        && lhs.vp.pMin == rhs.vp.pMin
        && lhs.vp.pMax == rhs.vp.pMax
        && lhs.vp.w == rhs.vp.w
        && lhs.vp.h == rhs.vp.h;
}

}  // namespace

bool ChartItem::shouldSkipHoverRecompute_(const QPointF& point, bool contextActive) const noexcept {
    if (!hoverActive_ || contextActive_ != contextActive) return false;
    const qreal dx = point.x() - hoverPoint_.x();
    const qreal dy = point.y() - hoverPoint_.y();
    return (dx * dx + dy * dy) < 0.25;
}

void ChartItem::setHoverPoint(qreal x, qreal y) {
    if (contextActive_) return;
    if (shouldSkipHoverRecompute_(QPointF{x, y}, false)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = false;
    updateHover_();
}

void ChartItem::activateContextPoint(qreal x, qreal y) {
    if (shouldSkipHoverRecompute_(QPointF{x, y}, true)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = true;
    updateHover_();
    if (hoveredTradeIndex_ < 0 && hoveredLiquidationIndex_ < 0 && hoveredBookKind_ == 0) {
        clearHover();
        return;
    }
    update();
}

void ChartItem::clearHover() {
    const bool hadHoverState = hoverActive_ || contextActive_ || hoveredTradeIndex_ >= 0 || hoveredLiquidationIndex_ >= 0 || hoveredBookKind_ != 0;
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredTradeTsNs_ = 0;
    hoveredTradePriceE8_ = 0;
    hoveredTradeQtyE8_ = 0;
    hoveredTradeSideBuy_ = true;
    hoveredLiquidationIndex_ = -1;
    hoveredLiquidationTsNs_ = 0;
    hoveredLiquidationPriceE8_ = 0;
    hoveredLiquidationQtyE8_ = 0;
    hoveredLiquidationAvgPriceE8_ = 0;
    hoveredLiquidationFilledQtyE8_ = 0;
    hoveredLiquidationSideBuy_ = true;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoveredBookTsStartNs_ = 0;
    hoveredBookTsEndNs_ = 0;
    if (hadHoverState) update();
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredTradeTsNs_ = 0;
    hoveredTradePriceE8_ = 0;
    hoveredTradeQtyE8_ = 0;
    hoveredTradeSideBuy_ = true;
    hoveredLiquidationIndex_ = -1;
    hoveredLiquidationTsNs_ = 0;
    hoveredLiquidationPriceE8_ = 0;
    hoveredLiquidationQtyE8_ = 0;
    hoveredLiquidationAvgPriceE8_ = 0;
    hoveredLiquidationFilledQtyE8_ = 0;
    hoveredLiquidationSideBuy_ = true;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoveredBookTsStartNs_ = 0;
    hoveredBookTsEndNs_ = 0;
    if (!hoverActive_ || !controller_ || width() <= 0 || height() <= 0) return;

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) return;

    HoverInfo hover{};
    if (cachedHitTestSnap_ != nullptr && cachedHitTestSnap_->loaded && sameViewport(*cachedHitTestSnap_, snap)) {
        hit_test::computeHover(*cachedHitTestSnap_, hoverPoint_, contextActive_, hover);
    } else {
        const RenderSnapshot hoverSnap = hoverSnapshotFrom(snap, cachedLiveSnap_.get());
        hit_test::computeHover(hoverSnap, hoverPoint_, contextActive_, hover);
    }

    hoveredTradeIndex_ = hover.tradeHit ? hover.tradeOrigIndex : -1;
    hoveredTradeTsNs_ = hover.tradeHit ? hover.tradeTsNs : 0;
    hoveredTradePriceE8_ = hover.tradeHit ? hover.tradePriceE8 : 0;
    hoveredTradeQtyE8_ = hover.tradeHit ? hover.tradeQtyE8 : 0;
    hoveredTradeSideBuy_ = hover.tradeSideBuy;
    hoveredLiquidationIndex_ = hover.liquidationHit ? hover.liquidationOrigIndex : -1;
    hoveredLiquidationTsNs_ = hover.liquidationHit ? hover.liquidationTsNs : 0;
    hoveredLiquidationPriceE8_ = hover.liquidationHit ? hover.liquidationPriceE8 : 0;
    hoveredLiquidationQtyE8_ = hover.liquidationHit ? hover.liquidationQtyE8 : 0;
    hoveredLiquidationAvgPriceE8_ = hover.liquidationHit ? hover.liquidationAvgPriceE8 : 0;
    hoveredLiquidationFilledQtyE8_ = hover.liquidationHit ? hover.liquidationFilledQtyE8 : 0;
    hoveredLiquidationSideBuy_ = hover.liquidationSideBuy;
    hoveredBookKind_ = hover.bookKind;
    hoveredBookPriceE8_ = hover.bookPriceE8;
    hoveredBookQtyE8_ = hover.bookQtyE8;
    hoveredBookTsNs_ = hover.bookTsNs;
    hoveredBookTsStartNs_ = hover.bookTsStartNs;
    hoveredBookTsEndNs_ = hover.bookTsEndNs;
}

}  // namespace hftrec::gui::viewer

namespace hftrec::gui::viewer::detail {

HoverInfo buildHoverInfo(const ChartItem& item) {
    HoverInfo hover{};
    hover.active = item.hoverActive_;
    hover.contextActive = item.contextActive_;
    hover.point = item.hoverPoint_;
    hover.bookKind = item.hoveredBookKind_;
    hover.bookPriceE8 = item.hoveredBookPriceE8_;
    hover.bookQtyE8 = item.hoveredBookQtyE8_;
    hover.bookTsNs = item.hoveredBookTsNs_;
    hover.bookTsStartNs = item.hoveredBookTsStartNs_;
    hover.bookTsEndNs = item.hoveredBookTsEndNs_;

    if (item.hoveredLiquidationIndex_ >= 0) {
        hover.liquidationHit = true;
        hover.liquidationOrigIndex = item.hoveredLiquidationIndex_;
        hover.liquidationTsNs = item.hoveredLiquidationTsNs_;
        hover.liquidationPriceE8 = item.hoveredLiquidationPriceE8_;
        hover.liquidationQtyE8 = item.hoveredLiquidationQtyE8_;
        hover.liquidationAvgPriceE8 = item.hoveredLiquidationAvgPriceE8_;
        hover.liquidationFilledQtyE8 = item.hoveredLiquidationFilledQtyE8_;
        hover.liquidationSideBuy = item.hoveredLiquidationSideBuy_;
    }

    if (item.hoveredTradeIndex_ < 0) {
        return hover;
    }

    hover.tradeHit = true;
    hover.tradeOrigIndex = item.hoveredTradeIndex_;
    hover.tradeTsNs = item.hoveredTradeTsNs_;
    hover.tradePriceE8 = item.hoveredTradePriceE8_;
    hover.tradeQtyE8 = item.hoveredTradeQtyE8_;
    hover.tradeSideBuy = item.hoveredTradeSideBuy_;
    return hover;
}

}  // namespace hftrec::gui::viewer::detail
