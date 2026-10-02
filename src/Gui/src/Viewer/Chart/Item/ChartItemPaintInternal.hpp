#pragma once

class QPainter;

using qreal = double;

namespace hftrec::gui::viewer {

class ChartController;
class ChartItem;
struct RenderSnapshot;

namespace detail {

void paintBookTickerLayer(QPainter* painter,
                          ChartController& controller,
                          const ChartItem& item,
                          const RenderSnapshot& settledSnapshot,
                          qreal width,
                          qreal height,
                          double dpr,
                          bool interactiveMode);

}  // namespace detail
}  // namespace hftrec::gui::viewer
