#include "BookTickerCompareInternal.hpp"

namespace hftrec::gui::viewer {
using namespace compare_detail;

void BookTickerCompareItem::setHoverPoint(qreal x, qreal y) {
    hoverActive_ = true;
    hoverPoint_ = QPointF{x, y};
    update();
}
void BookTickerCompareItem::clearHover() {
    hoverActive_ = false;
    update();
}
void BookTickerCompareItem::beginMeasure(qreal x, qreal y) {
    measureActive_ = true;
    measureVisible_ = true;
    measureStart_ = QPointF{x, y};
    measureEnd_ = measureStart_;
    update();
}
void BookTickerCompareItem::updateMeasure(qreal x, qreal y) {
    if (!measureActive_) return;
    measureEnd_ = QPointF{x, y};
    update();
}
void BookTickerCompareItem::endMeasure() {
    measureActive_ = false;
    update();
}
void BookTickerCompareItem::clearMeasure() {
    measureActive_ = false;
    measureVisible_ = false;
    update();
}
bool BookTickerCompareItem::isPricePanelPoint(qreal x, qreal y) const {
    const LayoutRects layout = makeLayout(boundingRect());
    return layout.primaryRect.contains(QPointF{x, y}) || layout.primaryScaleRect.contains(QPointF{x, y});
}
bool BookTickerCompareItem::isSpreadPanelPoint(qreal x, qreal y) const {
    const LayoutRects layout = makeLayout(boundingRect());
    return layout.spreadRect.contains(QPointF{x, y}) || layout.spreadScaleRect.contains(QPointF{x, y});
}
double BookTickerCompareItem::priceAnchorFraction(qreal y) const {
    const LayoutRects layout = makeLayout(boundingRect());
    const qreal height = std::max<qreal>(1.0, layout.primaryRect.height());
    const double fraction = static_cast<double>((layout.primaryRect.bottom() - y) / height);
    return std::clamp(fraction, 0.0, 1.0);
}
double BookTickerCompareItem::spreadAnchorFraction(qreal y) const {
    const LayoutRects layout = makeLayout(boundingRect());
    const qreal height = std::max<qreal>(1.0, layout.spreadRect.height());
    const double fraction = static_cast<double>((layout.spreadRect.bottom() - y) / height);
    return std::clamp(fraction, 0.0, 1.0);
}

}  // namespace hftrec::gui::viewer
