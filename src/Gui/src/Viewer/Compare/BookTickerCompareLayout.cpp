#include "BookTickerCompareInternal.hpp"

namespace hftrec::gui::viewer::compare_detail {

LayoutRects makeLayout(const QRectF& bounds) noexcept {
    const QRectF full = bounds.adjusted(kLeftMargin, kTopMargin, -12.0, -10.0);
    const qreal plotRight = full.right() - kRightScaleWidth;
    const qreal plotBottom = full.bottom() - kBottomScaleHeight;
    const qreal plotWidth = plotRight - full.left();
    const qreal availableHeight = std::max<qreal>(1.0, plotBottom - full.top());
    const qreal panelGap = std::min(kGap, availableHeight * 0.08);
    const qreal priceHeight = std::max<qreal>(24.0, (availableHeight - panelGap) * 0.66);
    LayoutRects layout{};
    layout.primaryRect = QRectF{full.left(), full.top(), plotWidth, priceHeight};
    layout.spreadRect = QRectF{full.left(), layout.primaryRect.bottom() + panelGap, plotWidth, plotBottom - layout.primaryRect.bottom() - panelGap};
    if (layout.spreadRect.height() < 24.0) layout.spreadRect.setHeight(24.0);
    layout.timeRect = QRectF{layout.primaryRect.left(), plotBottom, layout.primaryRect.width(), kBottomScaleHeight};
    layout.primaryScaleRect = QRectF{plotRight, layout.primaryRect.top(), kRightScaleWidth, layout.primaryRect.height()};
    layout.spreadScaleRect = QRectF{plotRight, layout.spreadRect.top(), kRightScaleWidth, layout.spreadRect.height()};
    return layout;
}

double xFor(std::int64_t ts, const Ranges& ranges, const QRectF& rect) noexcept {
    return rect.left() + (static_cast<double>(ts - ranges.tsMin) / static_cast<double>(ranges.tsMax - ranges.tsMin)) * rect.width();
}

std::int64_t tsForX(qreal x, const Ranges& ranges, const QRectF& rect) noexcept {
    const double fraction = std::clamp((x - rect.left()) / std::max<qreal>(1.0, rect.width()), 0.0, 1.0);
    return ranges.tsMin + static_cast<std::int64_t>(fraction * static_cast<double>(ranges.tsMax - ranges.tsMin));
}

double priceYFor(std::int64_t price, const Ranges& ranges, const QRectF& rect) noexcept {
    return rect.bottom() - (static_cast<double>(price - ranges.priceMin) / static_cast<double>(ranges.priceMax - ranges.priceMin)) * rect.height();
}

std::int64_t priceForY(qreal y, const Ranges& ranges, const QRectF& rect) noexcept {
    const double fraction = std::clamp((rect.bottom() - y) / std::max<qreal>(1.0, rect.height()), 0.0, 1.0);
    return ranges.priceMin + static_cast<std::int64_t>(fraction * static_cast<double>(ranges.priceMax - ranges.priceMin));
}

double spreadYFor(double spreadBps, const Ranges& ranges, const QRectF& rect) noexcept {
    return rect.bottom() - ((spreadBps - ranges.spreadMin) / (ranges.spreadMax - ranges.spreadMin)) * rect.height();
}

double spreadForY(qreal y, const Ranges& ranges, const QRectF& rect) noexcept {
    const double fraction = std::clamp((rect.bottom() - y) / std::max<qreal>(1.0, rect.height()), 0.0, 1.0);
    return ranges.spreadMin + fraction * (ranges.spreadMax - ranges.spreadMin);
}

const QRectF* rectForPoint(const QPointF& point, const LayoutRects& layout) noexcept {
    if (layout.primaryRect.contains(point)) return &layout.primaryRect;
    if (layout.spreadRect.contains(point)) return &layout.spreadRect;
    return nullptr;
}

}  // namespace hftrec::gui::viewer::compare_detail
