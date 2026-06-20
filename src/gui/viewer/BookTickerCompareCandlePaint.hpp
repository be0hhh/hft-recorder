#pragma once

#include <cstdint>
#include <vector>

#include "core/arbitrage/CandleSpread.hpp"
#include "core/replay/EventRows.hpp"

class QColor;
class QPainter;
class QRectF;

namespace hftrec::gui::viewer {

struct BookTickerCompareCandlePaintRanges {
    std::int64_t tsMin{0};
    std::int64_t tsMax{0};
    std::int64_t priceMin{0};
    std::int64_t priceMax{0};
    double spreadMin{0.0};
    double spreadMax{1.0};
};

void drawCompareCandleBodies(QPainter& painter,
                             const std::vector<hftrec::replay::CandleRow>& rows,
                             const BookTickerCompareCandlePaintRanges& ranges,
                             const QRectF& rect,
                             const QColor& sourceFillColor);

void drawCompareCandleSpreadTrace(QPainter& painter,
                                  const std::vector<hftrec::arbitrage::CandleSpreadPoint>& points,
                                  const BookTickerCompareCandlePaintRanges& ranges,
                                  const QRectF& rect,
                                  const QColor& sourceAColor,
                                  const QColor& sourceBColor);

}  // namespace hftrec::gui::viewer
