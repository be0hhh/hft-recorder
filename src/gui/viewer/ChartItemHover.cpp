#include "gui/viewer/ChartItem.hpp"

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace hftrec::gui::viewer {

void ChartItem::setHoverPoint(qreal x, qreal y) {
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = false;
    updateHover_();
    update();
}

void ChartItem::activateContextPoint(qreal x, qreal y) {
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = true;
    updateHover_();
    update();
}

void ChartItem::clearHover() {
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    update();
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    if (!hoverActive_ || !controller_ || !controller_->loaded() || width() <= 0 || height() <= 0) return;

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) return;

    HoverInfo hover{};
    hit_test::computeHover(snap, hoverPoint_, contextActive_, hover);

    hoveredTradeIndex_ = hover.tradeHit ? hover.tradeOrigIndex : -1;
    hoveredBookKind_ = hover.bookKind;
    hoveredBookPriceE8_ = hover.bookPriceE8;
    hoveredBookQtyE8_ = hover.bookQtyE8;
    hoveredBookTsNs_ = hover.bookTsNs;
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

    if (item.hoveredTradeIndex_ < 0 || item.controller_ == nullptr) {
        return hover;
    }

    const auto& trades = item.controller_->replay().trades();
    if (item.hoveredTradeIndex_ >= static_cast<int>(trades.size())) {
        return hover;
    }

    const auto& trade = trades[static_cast<std::size_t>(item.hoveredTradeIndex_)];
    hover.tradeHit = true;
    hover.tradeOrigIndex = item.hoveredTradeIndex_;
    hover.tradeTsNs = trade.tsNs;
    hover.tradePriceE8 = trade.priceE8;
    hover.tradeQtyE8 = trade.qtyE8;
    hover.tradeSideBuy = trade.sideBuy;
    return hover;
}

}  // namespace hftrec::gui::viewer::detail
