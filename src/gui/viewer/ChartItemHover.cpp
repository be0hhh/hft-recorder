#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <cmath>

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

qreal fundingStripY(const RenderSnapshot& snap) noexcept {
    if (snap.vp.h <= 36.0) return std::max<qreal>(8.0, snap.vp.h * 0.5);
    return std::clamp<qreal>(snap.vp.h - 18.0, 18.0, snap.vp.h - 8.0);
}

bool fundingStripHit(const RenderSnapshot& snap, const QPointF& point) noexcept {
    if (!snap.fundingVisible || snap.fundings.empty() || snap.vp.w <= 0.0 || snap.vp.h <= 0.0) return false;
    constexpr qreal kStripHitPx = 12.0;
    constexpr qreal kAxisReserveRight = 88.0;
    const qreal stripLeft = 8.0;
    const qreal stripRight = std::max<qreal>(stripLeft, snap.vp.w - kAxisReserveRight);
    const qreal y = fundingStripY(snap);
    return point.x() >= stripLeft - kStripHitPx
        && point.x() <= stripRight + kStripHitPx
        && std::abs(point.y() - y) <= kStripHitPx;
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
    if (hoveredTradeIndex_ < 0 && hoveredLiquidationIndex_ < 0 && !hoveredStrategyFill_ && hoveredBookKind_ == 0 && !hoveredFunding_) {
        const RenderSnapshot& snap = ensureSnapshot_();
        if (!fundingStripHit(snap, hoverPoint_)) {
            clearHover();
            return;
        }
    }
    update();
}

void ChartItem::clearHover() {
    const bool hadHoverState = hoverActive_ || contextActive_ || hoveredTradeIndex_ >= 0 || hoveredLiquidationIndex_ >= 0 || hoveredStrategyFill_ || hoveredBookKind_ != 0 || hoveredFunding_;
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredTradeTsNs_ = 0;
    hoveredTradePriceE8_ = 0;
    hoveredTradeQtyE8_ = 0;
    hoveredTradeTotalQtyE8_ = 0;
    hoveredTradeTotalAmountE8_ = 0;
    hoveredTradeTsStartNs_ = 0;
    hoveredTradeTsEndNs_ = 0;
    hoveredTradeCount_ = 0;
    hoveredTradeBuyQtyE8_ = 0;
    hoveredTradeSellQtyE8_ = 0;
    hoveredTradeBuyAmountE8_ = 0;
    hoveredTradeSellAmountE8_ = 0;
    hoveredTradeRepresentativePriceE8_ = 0;
    hoveredTradeAggregated_ = false;
    hoveredTradeSideBuy_ = true;
    hoveredTradeGroupEntries_.clear();
    hoveredLiquidationIndex_ = -1;
    hoveredLiquidationTsNs_ = 0;
    hoveredLiquidationPriceE8_ = 0;
    hoveredLiquidationQtyE8_ = 0;
    hoveredLiquidationAvgPriceE8_ = 0;
    hoveredLiquidationFilledQtyE8_ = 0;
    hoveredLiquidationSideBuy_ = true;
    hoveredStrategyFill_ = false;
    hoveredStrategyFillTsNs_ = 0;
    hoveredStrategyFillPriceE8_ = 0;
    hoveredStrategyFillQtyE8_ = 0;
    hoveredStrategyFillAmountE8_ = 0;
    hoveredStrategyFillSideBuy_ = true;
    hoveredStrategyFillReduceOnly_ = false;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoveredBookTsStartNs_ = 0;
    hoveredBookTsEndNs_ = 0;
    hoveredFunding_ = false;
    hoveredFundingEventTsNs_ = 0;
    hoveredFundingRateE8_ = 0;
    hoveredFundingTsNs_ = 0;
    hoveredNextFundingTsNs_ = 0;
    hoveredFundingCadenceNs_ = 0;
    if (hadHoverState) update();
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredTradeTsNs_ = 0;
    hoveredTradePriceE8_ = 0;
    hoveredTradeQtyE8_ = 0;
    hoveredTradeTotalQtyE8_ = 0;
    hoveredTradeTotalAmountE8_ = 0;
    hoveredTradeTsStartNs_ = 0;
    hoveredTradeTsEndNs_ = 0;
    hoveredTradeCount_ = 0;
    hoveredTradeBuyQtyE8_ = 0;
    hoveredTradeSellQtyE8_ = 0;
    hoveredTradeBuyAmountE8_ = 0;
    hoveredTradeSellAmountE8_ = 0;
    hoveredTradeRepresentativePriceE8_ = 0;
    hoveredTradeAggregated_ = false;
    hoveredTradeSideBuy_ = true;
    hoveredTradeGroupEntries_.clear();
    hoveredLiquidationIndex_ = -1;
    hoveredLiquidationTsNs_ = 0;
    hoveredLiquidationPriceE8_ = 0;
    hoveredLiquidationQtyE8_ = 0;
    hoveredLiquidationAvgPriceE8_ = 0;
    hoveredLiquidationFilledQtyE8_ = 0;
    hoveredLiquidationSideBuy_ = true;
    hoveredStrategyFill_ = false;
    hoveredStrategyFillTsNs_ = 0;
    hoveredStrategyFillPriceE8_ = 0;
    hoveredStrategyFillQtyE8_ = 0;
    hoveredStrategyFillAmountE8_ = 0;
    hoveredStrategyFillSideBuy_ = true;
    hoveredStrategyFillReduceOnly_ = false;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoveredBookTsStartNs_ = 0;
    hoveredBookTsEndNs_ = 0;
    hoveredFunding_ = false;
    hoveredFundingEventTsNs_ = 0;
    hoveredFundingRateE8_ = 0;
    hoveredFundingTsNs_ = 0;
    hoveredNextFundingTsNs_ = 0;
    hoveredFundingCadenceNs_ = 0;
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
    hoveredTradeTotalQtyE8_ = hover.tradeHit ? hover.tradeTotalQtyE8 : 0;
    hoveredTradeTotalAmountE8_ = hover.tradeHit ? hover.tradeTotalAmountE8 : 0;
    hoveredTradeTsStartNs_ = hover.tradeHit ? hover.tradeTsStartNs : 0;
    hoveredTradeTsEndNs_ = hover.tradeHit ? hover.tradeTsEndNs : 0;
    hoveredTradeCount_ = hover.tradeHit ? hover.tradeCount : 0;
    hoveredTradeBuyQtyE8_ = hover.tradeHit ? hover.tradeBuyQtyE8 : 0;
    hoveredTradeSellQtyE8_ = hover.tradeHit ? hover.tradeSellQtyE8 : 0;
    hoveredTradeBuyAmountE8_ = hover.tradeHit ? hover.tradeBuyAmountE8 : 0;
    hoveredTradeSellAmountE8_ = hover.tradeHit ? hover.tradeSellAmountE8 : 0;
    hoveredTradeRepresentativePriceE8_ = hover.tradeHit ? hover.tradeRepresentativePriceE8 : 0;
    hoveredTradeAggregated_ = hover.tradeHit && hover.tradeAggregated;
    hoveredTradeSideBuy_ = hover.tradeSideBuy;
    hoveredTradeGroupEntries_ = hover.tradeHit ? hover.tradeGroupEntries : std::vector<TradeGroupEntry>{};
    hoveredLiquidationIndex_ = hover.liquidationHit ? hover.liquidationOrigIndex : -1;
    hoveredLiquidationTsNs_ = hover.liquidationHit ? hover.liquidationTsNs : 0;
    hoveredLiquidationPriceE8_ = hover.liquidationHit ? hover.liquidationPriceE8 : 0;
    hoveredLiquidationQtyE8_ = hover.liquidationHit ? hover.liquidationQtyE8 : 0;
    hoveredLiquidationAvgPriceE8_ = hover.liquidationHit ? hover.liquidationAvgPriceE8 : 0;
    hoveredLiquidationFilledQtyE8_ = hover.liquidationHit ? hover.liquidationFilledQtyE8 : 0;
    hoveredLiquidationSideBuy_ = hover.liquidationSideBuy;
    hoveredStrategyFill_ = hover.strategyFillHit;
    hoveredStrategyFillTsNs_ = hover.strategyFillHit ? hover.strategyFillTsNs : 0;
    hoveredStrategyFillPriceE8_ = hover.strategyFillHit ? hover.strategyFillPriceE8 : 0;
    hoveredStrategyFillQtyE8_ = hover.strategyFillHit ? hover.strategyFillQtyE8 : 0;
    hoveredStrategyFillAmountE8_ = hover.strategyFillHit ? hover.strategyFillAmountE8 : 0;
    hoveredStrategyFillSideBuy_ = hover.strategyFillSideBuy;
    hoveredStrategyFillReduceOnly_ = hover.strategyFillReduceOnly;
    hoveredBookKind_ = hover.bookKind;
    hoveredBookPriceE8_ = hover.bookPriceE8;
    hoveredBookQtyE8_ = hover.bookQtyE8;
    hoveredBookTsNs_ = hover.bookTsNs;
    hoveredBookTsStartNs_ = hover.bookTsStartNs;
    hoveredBookTsEndNs_ = hover.bookTsEndNs;
    hoveredFunding_ = hover.fundingHit;
    hoveredFundingEventTsNs_ = hover.fundingHit ? hover.fundingEventTsNs : 0;
    hoveredFundingRateE8_ = hover.fundingHit ? hover.fundingRateE8 : 0;
    hoveredFundingTsNs_ = hover.fundingHit ? hover.fundingTsNs : 0;
    hoveredNextFundingTsNs_ = hover.fundingHit ? hover.nextFundingTsNs : 0;
    hoveredFundingCadenceNs_ = hover.fundingHit ? hover.fundingCadenceNs : 0;
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
    if (item.hoveredFunding_) {
        hover.fundingHit = true;
        hover.fundingEventTsNs = item.hoveredFundingEventTsNs_;
        hover.fundingRateE8 = item.hoveredFundingRateE8_;
        hover.fundingTsNs = item.hoveredFundingTsNs_;
        hover.nextFundingTsNs = item.hoveredNextFundingTsNs_;
        hover.fundingCadenceNs = item.hoveredFundingCadenceNs_;
    }

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

    if (item.hoveredStrategyFill_) {
        hover.strategyFillHit = true;
        hover.strategyFillTsNs = item.hoveredStrategyFillTsNs_;
        hover.strategyFillPriceE8 = item.hoveredStrategyFillPriceE8_;
        hover.strategyFillQtyE8 = item.hoveredStrategyFillQtyE8_;
        hover.strategyFillAmountE8 = item.hoveredStrategyFillAmountE8_;
        hover.strategyFillSideBuy = item.hoveredStrategyFillSideBuy_;
        hover.strategyFillReduceOnly = item.hoveredStrategyFillReduceOnly_;
    }

    if (item.hoveredTradeIndex_ < 0) {
        return hover;
    }

    hover.tradeHit = true;
    hover.tradeOrigIndex = item.hoveredTradeIndex_;
    hover.tradeTsNs = item.hoveredTradeTsNs_;
    hover.tradePriceE8 = item.hoveredTradePriceE8_;
    hover.tradeQtyE8 = item.hoveredTradeQtyE8_;
    hover.tradeTotalQtyE8 = item.hoveredTradeTotalQtyE8_;
    hover.tradeTotalAmountE8 = item.hoveredTradeTotalAmountE8_;
    hover.tradeTsStartNs = item.hoveredTradeTsStartNs_;
    hover.tradeTsEndNs = item.hoveredTradeTsEndNs_;
    hover.tradeCount = item.hoveredTradeCount_;
    hover.tradeBuyQtyE8 = item.hoveredTradeBuyQtyE8_;
    hover.tradeSellQtyE8 = item.hoveredTradeSellQtyE8_;
    hover.tradeBuyAmountE8 = item.hoveredTradeBuyAmountE8_;
    hover.tradeSellAmountE8 = item.hoveredTradeSellAmountE8_;
    hover.tradeRepresentativePriceE8 = item.hoveredTradeRepresentativePriceE8_;
    hover.tradeAggregated = item.hoveredTradeAggregated_;
    hover.tradeSideBuy = item.hoveredTradeSideBuy_;
    hover.tradeGroupEntries = item.hoveredTradeGroupEntries_;
    return hover;
}

}  // namespace hftrec::gui::viewer::detail
