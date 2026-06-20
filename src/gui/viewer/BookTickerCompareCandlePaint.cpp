#include "gui/viewer/BookTickerCompareCandlePaint.hpp"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

namespace hftrec::gui::viewer {

namespace {

double xFor(std::int64_t ts, const BookTickerCompareCandlePaintRanges& ranges, const QRectF& rect) noexcept {
    return rect.left() + (static_cast<double>(ts - ranges.tsMin) / static_cast<double>(ranges.tsMax - ranges.tsMin)) * rect.width();
}

double priceYFor(std::int64_t price, const BookTickerCompareCandlePaintRanges& ranges, const QRectF& rect) noexcept {
    return rect.bottom() - (static_cast<double>(price - ranges.priceMin) / static_cast<double>(ranges.priceMax - ranges.priceMin)) * rect.height();
}

double spreadYFor(double spreadBps, const BookTickerCompareCandlePaintRanges& ranges, const QRectF& rect) noexcept {
    return rect.bottom() - ((spreadBps - ranges.spreadMin) / (ranges.spreadMax - ranges.spreadMin)) * rect.height();
}

std::int64_t candleDurationNs(const hftrec::replay::CandleRow& row) noexcept {
    if (row.durationNs > 0) return row.durationNs;
    if (row.tier == 1) return 60ll * 1000000000ll;
    if (row.tier == 2) return 15ll * 60ll * 1000000000ll;
    if (row.tier == 3) return 24ll * 60ll * 60ll * 1000000000ll;
    return 60ll * 1000000000ll;
}

QColor colorForDirection(hftrec::arbitrage::SpreadDirection direction, const QColor& sourceAColor, const QColor& sourceBColor) {
    if (direction == hftrec::arbitrage::SpreadDirection::BuyAAskSellBBid) return sourceAColor;
    if (direction == hftrec::arbitrage::SpreadDirection::BuyBAskSellABid) return sourceBColor;
    return QColor{155, 155, 162};
}

bool canConnect(const hftrec::arbitrage::CandleSpreadPoint& previous,
                const hftrec::arbitrage::CandleSpreadPoint& current) noexcept {
    const std::int64_t duration = std::max(previous.durationNs, current.durationNs);
    if (duration <= 0) return false;
    return (current.tsNs - previous.tsNs) <= duration + (duration / 2);
}

}  // namespace

void drawCompareCandleBodies(QPainter& painter,
                             const std::vector<hftrec::replay::CandleRow>& rows,
                             const BookTickerCompareCandlePaintRanges& ranges,
                             const QRectF& rect,
                             const QColor& sourceFillColor) {
    for (const auto& row : rows) {
        if (!row.hasOhlc) continue;
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        if (row.openE8 <= 0 || row.highE8 <= 0 || row.lowE8 <= 0 || row.closeE8 <= 0 || row.highE8 < row.lowE8) continue;
        const qreal x = xFor(row.tsNs, ranges, rect);
        const qreal nextX = xFor(std::min<std::int64_t>(row.tsNs + candleDurationNs(row), ranges.tsMax), ranges, rect);
        const qreal width = std::clamp(std::abs(nextX - x) * 0.65, 3.0, 14.0);
        const qreal yHigh = priceYFor(row.highE8, ranges, rect);
        const qreal yLow = priceYFor(row.lowE8, ranges, rect);
        const qreal yOpen = priceYFor(row.openE8, ranges, rect);
        const qreal yClose = priceYFor(row.closeE8, ranges, rect);
        if ((x + width) < rect.left() || (x - width) > rect.right()) continue;

        QColor color = sourceFillColor;
        color.setAlpha(235);
        QPen pen{color};
        pen.setWidth(1);
        pen.setCapStyle(Qt::SquareCap);
        painter.setPen(pen);
        painter.drawLine(QPointF{x, yHigh}, QPointF{x, yLow});

        QColor fill = color;
        fill.setAlpha(125);
        painter.setBrush(fill);
        const qreal top = std::min(yOpen, yClose);
        const qreal bottom = std::max(yOpen, yClose);
        painter.drawRect(QRectF{x - width * 0.5, top, width, std::max<qreal>(2.0, bottom - top)});
    }
    painter.setBrush(Qt::NoBrush);
}

void drawCompareCandleSpreadTrace(QPainter& painter,
                                  const std::vector<hftrec::arbitrage::CandleSpreadPoint>& points,
                                  const BookTickerCompareCandlePaintRanges& ranges,
                                  const QRectF& rect,
                                  const QColor& sourceAColor,
                                  const QColor& sourceBColor) {
    if (points.empty()) return;
    painter.setBrush(Qt::NoBrush);

    const hftrec::arbitrage::CandleSpreadPoint* previous = nullptr;
    QPointF previousPoint{};
    for (const auto& point : points) {
        if (point.tsNs < ranges.tsMin) {
            previous = &point;
            previousPoint = QPointF{xFor(point.tsNs, ranges, rect), spreadYFor(point.spreadBps, ranges, rect)};
            continue;
        }
        if (point.tsNs > ranges.tsMax) break;
        const QPointF current{xFor(point.tsNs, ranges, rect), spreadYFor(point.spreadBps, ranges, rect)};
        if (previous != nullptr && previous->tsNs >= ranges.tsMin && canConnect(*previous, point)) {
            QPen pen{colorForDirection(point.direction, sourceAColor, sourceBColor)};
            pen.setWidth(2);
            pen.setCapStyle(Qt::SquareCap);
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            painter.drawLine(previousPoint, current);
        }
        previous = &point;
        previousPoint = current;
    }
}

}  // namespace hftrec::gui::viewer
