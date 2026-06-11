#include "gui/viewer/BookTickerCompareItem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include "gui/viewer/BookTickerCompareController.hpp"

namespace hftrec::gui::viewer {

namespace {

constexpr double kE8 = 100000000.0;
constexpr qreal kLeftMargin = 12.0;
constexpr qreal kTopMargin = 10.0;
constexpr qreal kRightScaleWidth = 118.0;
constexpr qreal kBottomScaleHeight = 28.0;
constexpr qreal kGap = 12.0;

struct Ranges {
    std::int64_t tsMin{0};
    std::int64_t tsMax{0};
    std::int64_t currentTs{0};
    std::int64_t priceMin{0};
    std::int64_t priceMax{0};
    double spreadMin{0.0};
    double spreadMax{1.0};
    double rawSpreadMax{0.0};
    double internalPenaltyAvg{0.0};
    double feePenaltyAvg{0.0};
    double meanAvg{0.0};
    double deviationAbsMax{0.0};
    double costBandMax{0.0};
    double edgeAfterCostMax{0.0};
    double totalFeesBps{0.0};
};

struct LayoutRects {
    QRectF primaryRect{};
    QRectF spreadRect{};
    QRectF timeRect{};
    QRectF primaryScaleRect{};
    QRectF spreadScaleRect{};
};

void absorbPrice(const hftrec::replay::BookTickerRow& row, Ranges& ranges, bool& hasPrice) noexcept {
    if (row.bidPriceE8 <= 0 || row.askPriceE8 <= 0) return;
    if (!hasPrice) {
        ranges.priceMin = std::min(row.bidPriceE8, row.askPriceE8);
        ranges.priceMax = std::max(row.bidPriceE8, row.askPriceE8);
        hasPrice = true;
        return;
    }
    ranges.priceMin = std::min(ranges.priceMin, std::min(row.bidPriceE8, row.askPriceE8));
    ranges.priceMax = std::max(ranges.priceMax, std::max(row.bidPriceE8, row.askPriceE8));
}

Ranges computeRanges(const std::vector<hftrec::replay::BookTickerRow>& a,
                     const std::vector<hftrec::replay::BookTickerRow>& b,
                     const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreads,
                     const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& means,
                     std::int64_t tsMin,
                     std::int64_t tsMax,
                     std::int64_t currentTs,
                     double totalFeesBps) noexcept {
    Ranges ranges{};
    ranges.tsMin = tsMin;
    ranges.tsMax = tsMax > tsMin ? tsMax : tsMin + 1;
    ranges.currentTs = currentTs;
    ranges.totalFeesBps = totalFeesBps;
    bool hasPrice = false;
    auto absorbCarryPrice = [&](const std::vector<hftrec::replay::BookTickerRow>& rows) noexcept {
        const hftrec::replay::BookTickerRow* carry = nullptr;
        for (const auto& row : rows) {
            if (row.tsNs > ranges.tsMin) break;
            if (row.bidPriceE8 > 0 && row.askPriceE8 > 0) carry = &row;
        }
        if (carry != nullptr) absorbPrice(*carry, ranges, hasPrice);
    };
    absorbCarryPrice(a);
    absorbCarryPrice(b);
    for (const auto& row : a) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbPrice(row, ranges, hasPrice);
    }
    for (const auto& row : b) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbPrice(row, ranges, hasPrice);
    }
    bool hasSpread = false;
    double penaltySum = 0.0;
    double feePenaltySum = 0.0;
    std::size_t penaltyCount = 0u;
    const hftrec::arbitrage::BookTickerSpreadPoint* carrySpread = nullptr;
    for (const auto& point : spreads) {
        if (point.tsNs > ranges.tsMin) break;
        carrySpread = &point;
    }
    if (carrySpread != nullptr) {
        ranges.spreadMin = std::min(0.0, carrySpread->spreadBps);
        ranges.spreadMax = std::max(1.0, carrySpread->spreadBps);
        ranges.rawSpreadMax = carrySpread->rawSpreadBps;
        hasSpread = true;
    }
    for (const auto& point : spreads) {
        if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
        if (!hasSpread) {
            ranges.spreadMin = std::min(0.0, point.spreadBps);
            ranges.spreadMax = std::max(1.0, point.spreadBps);
            ranges.rawSpreadMax = point.rawSpreadBps;
            hasSpread = true;
        } else {
            ranges.spreadMin = std::min(ranges.spreadMin, point.spreadBps);
            ranges.spreadMax = std::max(ranges.spreadMax, point.spreadBps);
            ranges.rawSpreadMax = std::max(ranges.rawSpreadMax, point.rawSpreadBps);
        }
        penaltySum += point.internalPenaltyBps;
        feePenaltySum += point.feePenaltyBps;
        ++penaltyCount;
    }
    if (penaltyCount > 0u) ranges.internalPenaltyAvg = penaltySum / static_cast<double>(penaltyCount);
    if (penaltyCount > 0u) ranges.feePenaltyAvg = feePenaltySum / static_cast<double>(penaltyCount);
    double meanSum = 0.0;
    std::size_t meanCount = 0u;
    for (const auto& point : means) {
        if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
        if (!hasSpread) {
            ranges.spreadMin = std::min(point.meanBps - point.costBandBps, point.meanBps);
            ranges.spreadMax = std::max(point.meanBps + point.costBandBps, point.meanBps);
            hasSpread = true;
        } else {
            ranges.spreadMin = std::min(ranges.spreadMin, point.meanBps - point.costBandBps);
            ranges.spreadMax = std::max(ranges.spreadMax, point.meanBps + point.costBandBps);
        }
        meanSum += point.meanBps;
        ranges.costBandMax = std::max(ranges.costBandMax, point.costBandBps);
        ranges.deviationAbsMax = std::max(ranges.deviationAbsMax, std::abs(point.deviationBps));
        ranges.edgeAfterCostMax = std::max(ranges.edgeAfterCostMax, point.edgeAfterCostBps);
        ++meanCount;
    }
    if (meanCount > 0u) ranges.meanAvg = meanSum / static_cast<double>(meanCount);
    if (!hasPrice) {
        for (const auto& row : a) absorbPrice(row, ranges, hasPrice);
        for (const auto& row : b) absorbPrice(row, ranges, hasPrice);
    }
    if (ranges.priceMax <= ranges.priceMin) ranges.priceMax = ranges.priceMin + 1;
    const auto pricePad = std::max<std::int64_t>(1, (ranges.priceMax - ranges.priceMin) / 20);
    ranges.priceMin -= pricePad;
    ranges.priceMax += pricePad;
    if (std::abs(ranges.spreadMax - ranges.spreadMin) < 0.000001) ranges.spreadMax = ranges.spreadMin + 1.0;
    const double spreadPad = std::max(0.25, (ranges.spreadMax - ranges.spreadMin) * 0.10);
    ranges.spreadMin -= spreadPad;
    ranges.spreadMax += spreadPad;
    ranges.spreadMin = std::min(ranges.spreadMin, 0.0);
    ranges.spreadMax = std::max(ranges.spreadMax, 1.0);
    return ranges;
}

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

void drawPolyline(QPainter& painter, const QPolygonF& points, const QColor& color, int width = 1) {
    if (points.size() < 2) return;
    QPen pen{color};
    pen.setWidth(width);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(points);
}

void appendTickerStepPoints(const std::vector<hftrec::replay::BookTickerRow>& rows,
                            bool bid,
                            const Ranges& ranges,
                            const QRectF& rect,
                            QPolygonF& out) {
    out.clear();
    out.reserve(static_cast<int>(std::min<std::size_t>(rows.size() * 2u + 2u, 1000000)));

    const hftrec::replay::BookTickerRow* carry = nullptr;
    for (const auto& row : rows) {
        if (row.tsNs > ranges.tsMin) break;
        const auto price = bid ? row.bidPriceE8 : row.askPriceE8;
        if (price > 0) carry = &row;
    }
    if (carry != nullptr) {
        const auto price = bid ? carry->bidPriceE8 : carry->askPriceE8;
        out.push_back(QPointF{rect.left(), priceYFor(price, ranges, rect)});
    }
    bool havePrev = carry != nullptr;
    QPointF prev = havePrev ? out.back() : QPointF{};
    for (const auto& row : rows) {
        if (row.tsNs < ranges.tsMin) continue;
        if (row.tsNs > ranges.tsMax) break;
        const auto price = bid ? row.bidPriceE8 : row.askPriceE8;
        if (price <= 0) continue;
        const QPointF current{xFor(row.tsNs, ranges, rect), priceYFor(price, ranges, rect)};
        if (havePrev && current.x() > prev.x()) out.push_back(QPointF{current.x(), prev.y()});
        out.push_back(current);
        prev = current;
        havePrev = true;
    }
    const std::int64_t holdTs = std::min(ranges.tsMax, ranges.currentTs);
    if (havePrev && holdTs > ranges.tsMin) {
        const qreal holdX = xFor(holdTs, ranges, rect);
        if (holdX > prev.x()) out.push_back(QPointF{holdX, prev.y()});
    }
}
void drawSpreadSegments(QPainter& painter,
                        const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreads,
                        const Ranges& ranges,
                        const QRectF& rect) {
    const hftrec::arbitrage::BookTickerSpreadPoint* carry = nullptr;
    for (const auto& point : spreads) {
        if (point.tsNs > ranges.tsMin) break;
        carry = &point;
    }

    bool havePrev = false;
    QPointF prev{};
    hftrec::arbitrage::SpreadDirection prevDirection = hftrec::arbitrage::SpreadDirection::None;
    if (carry != nullptr) {
        prev = QPointF{rect.left(), spreadYFor(carry->spreadBps, ranges, rect)};
        prevDirection = carry->direction;
        havePrev = true;
    }

    auto drawSegment = [&](const QPointF& from, const QPointF& to, hftrec::arbitrage::SpreadDirection direction) {
        const QColor color = direction == hftrec::arbitrage::SpreadDirection::BuyAAskSellBBid
                                 ? QColor{58, 150, 255}
                                 : QColor{255, 82, 96};
        QPen pen{color};
        pen.setWidth(1);
        pen.setCapStyle(Qt::SquareCap);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.drawLine(from, to);
    };

    for (const auto& point : spreads) {
        if (point.tsNs < ranges.tsMin) continue;
        if (point.tsNs > ranges.tsMax) break;
        const QPointF current{xFor(point.tsNs, ranges, rect), spreadYFor(point.spreadBps, ranges, rect)};
        if (havePrev) {
            const auto direction = point.direction == hftrec::arbitrage::SpreadDirection::None ? prevDirection : point.direction;
            const QPointF corner{current.x(), prev.y()};
            drawSegment(prev, corner, direction);
            drawSegment(corner, current, direction);
        }
        prev = current;
        prevDirection = point.direction;
        havePrev = true;
    }
    const std::int64_t holdTs = std::min(ranges.tsMax, ranges.currentTs);
    if (havePrev && holdTs > ranges.tsMin) {
        const QPointF holdPoint{xFor(holdTs, ranges, rect), prev.y()};
        if (holdPoint.x() > prev.x()) drawSegment(prev, holdPoint, prevDirection);
    }
}

void drawMeanBands(QPainter& painter,
                   const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& means,
                   const Ranges& ranges,
                   const QRectF& rect) {
    if (means.size() < 2) return;
    QPolygonF meanLine;
    QPolygonF upperBand;
    QPolygonF lowerBand;
    meanLine.reserve(static_cast<int>(std::min<std::size_t>(means.size() + 2, 1000000)));
    upperBand.reserve(meanLine.capacity());
    lowerBand.reserve(meanLine.capacity());

    const hftrec::arbitrage::BookTickerSpreadMeanPoint* carry = nullptr;
    for (const auto& point : means) {
        if (point.tsNs > ranges.tsMin) break;
        carry = &point;
    }
    auto append = [&](std::int64_t ts, double mean, double costBandBps) {
        const double x = xFor(ts, ranges, rect);
        meanLine.push_back(QPointF{x, spreadYFor(mean, ranges, rect)});
        upperBand.push_back(QPointF{x, spreadYFor(mean + costBandBps, ranges, rect)});
        lowerBand.push_back(QPointF{x, spreadYFor(mean - costBandBps, ranges, rect)});
    };
    if (carry != nullptr) append(ranges.tsMin, carry->meanBps, carry->costBandBps);
    for (const auto& point : means) {
        if (point.tsNs < ranges.tsMin) continue;
        if (point.tsNs > ranges.tsMax) break;
        append(point.tsNs, point.meanBps, point.costBandBps);
    }

    QPen bandPen{QColor{145, 145, 150}};
    bandPen.setWidth(1);
    bandPen.setStyle(Qt::DashLine);
    bandPen.setCapStyle(Qt::SquareCap);
    painter.setPen(bandPen);
    painter.drawPolyline(upperBand);
    painter.drawPolyline(lowerBand);

    QPen meanPen{QColor{255, 214, 84}};
    meanPen.setWidth(1);
    meanPen.setCapStyle(Qt::SquareCap);
    meanPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(meanPen);
    painter.drawPolyline(meanLine);
}

QColor overlayColor(std::uint32_t legIndex, bool buy) noexcept {
    if (legIndex == 1u) return buy ? QColor{76, 212, 126} : QColor{179, 102, 255};
    return buy ? QColor{36, 194, 203} : QColor{218, 37, 54};
}

void drawStrategyOverlay(QPainter& painter,
                         const StrategyOverlayData& overlay,
                         const Ranges& ranges,
                         const QRectF& rect) {
    for (const auto& segment : overlay.orderSegments) {
        if (segment.tsEndNs < ranges.tsMin || segment.tsStartNs > ranges.tsMax) continue;
        if (segment.priceE8 < ranges.priceMin || segment.priceE8 > ranges.priceMax) continue;
        const qreal x0 = xFor(std::max<std::int64_t>(segment.tsStartNs, ranges.tsMin), ranges, rect);
        const qreal x1 = xFor(std::min<std::int64_t>(segment.tsEndNs, ranges.tsMax), ranges, rect);
        const qreal y = priceYFor(segment.priceE8, ranges, rect);
        QPen pen{overlayColor(segment.legIndex, segment.sideBuy)};
        pen.setWidth(segment.legIndex == 1u ? 2 : 1);
        if (segment.openEnded) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF{x0, y}, QPointF{x1, y});
    }

    for (const auto& marker : overlay.fillMarkers) {
        if (marker.tsNs < ranges.tsMin || marker.tsNs > ranges.tsMax) continue;
        if (marker.priceE8 < ranges.priceMin || marker.priceE8 > ranges.priceMax) continue;
        const qreal x = xFor(marker.tsNs, ranges, rect);
        const qreal y = priceYFor(marker.priceE8, ranges, rect);
        const qreal r = marker.legIndex == 1u ? 5.0 : 4.0;
        QPolygonF triangle;
        if (marker.shape == StrategyFillShape::BuyUp) {
            triangle << QPointF{x, y - r} << QPointF{x - r, y + r} << QPointF{x + r, y + r};
        } else {
            triangle << QPointF{x, y + r} << QPointF{x - r, y - r} << QPointF{x + r, y - r};
        }
        painter.setPen(QPen{QColor{18, 18, 21}, 1});
        painter.setBrush(overlayColor(marker.legIndex, marker.sideBuy));
        painter.drawPolygon(triangle);
    }
}

const hftrec::arbitrage::BookTickerSpreadPoint* nearestSpreadPoint(
    const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& points,
    std::int64_t ts) noexcept {
    if (points.empty()) return nullptr;
    auto it = std::lower_bound(points.begin(), points.end(), ts, [](const auto& point, std::int64_t value) {
        return point.tsNs < value;
    });
    if (it == points.begin()) return &*it;
    if (it == points.end()) return &points.back();
    const auto* right = &*it;
    const auto* left = &*(it - 1);
    return (ts - left->tsNs) <= (right->tsNs - ts) ? left : right;
}

const hftrec::arbitrage::BookTickerSpreadMeanPoint* nearestMeanPoint(
    const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& points,
    std::int64_t ts) noexcept {
    if (points.empty()) return nullptr;
    auto it = std::lower_bound(points.begin(), points.end(), ts, [](const auto& point, std::int64_t value) {
        return point.tsNs < value;
    });
    if (it == points.begin()) return &*it;
    if (it == points.end()) return &points.back();
    const auto* right = &*it;
    const auto* left = &*(it - 1);
    return (ts - left->tsNs) <= (right->tsNs - ts) ? left : right;
}
QString formatPrice(std::int64_t priceE8) {
    const double value = static_cast<double>(priceE8) / kE8;
    const int decimals = value >= 1000.0 ? 2 : 6;
    return QString::number(value, 'f', decimals);
}

QString formatBps(double bps) {
    return QString::number(bps, 'f', 2) + QStringLiteral(" bps");
}

QString formatDurationNs(std::int64_t ns) {
    const double absNs = std::abs(static_cast<double>(ns));
    if (absNs < 1000000.0) return QString::number(static_cast<double>(ns) / 1000.0, 'f', 1) + QStringLiteral(" us");
    if (absNs < 1000000000.0) return QString::number(static_cast<double>(ns) / 1000000.0, 'f', 2) + QStringLiteral(" ms");
    return QString::number(static_cast<double>(ns) / 1000000000.0, 'f', 3) + QStringLiteral(" s");
}

QString formatTimeOffset(std::int64_t ts, const Ranges& ranges) {
    return QStringLiteral("+") + formatDurationNs(ts - ranges.tsMin);
}

void drawAxisTicks(QPainter& painter, const QRectF& plotRect, const QRectF& scaleRect, const Ranges& ranges, bool priceAxis) {
    const int ticks = priceAxis ? 5 : 4;
    painter.setFont(QFont{painter.font().family(), 10});
    const QColor grid{58, 58, 64};
    const QColor tick{224, 224, 230};
    for (int i = 0; i <= ticks; ++i) {
        const double f = static_cast<double>(i) / static_cast<double>(ticks);
        const qreal y = plotRect.bottom() - f * plotRect.height();
        QPen gridPen{grid};
        gridPen.setStyle(Qt::DashLine);
        painter.setPen(gridPen);
        painter.drawLine(QPointF{plotRect.left(), y}, QPointF{plotRect.right(), y});
        painter.setPen(tick);
        painter.drawLine(QPointF{plotRect.right(), y}, QPointF{plotRect.right() + 5.0, y});
        QString label;
        if (priceAxis) {
            const auto value = ranges.priceMin + static_cast<std::int64_t>(f * static_cast<double>(ranges.priceMax - ranges.priceMin));
            label = formatPrice(value);
        } else {
            const double value = ranges.spreadMin + f * (ranges.spreadMax - ranges.spreadMin);
            label = QString::number(value, 'f', 2);
        }
        const QRectF labelRect = scaleRect.adjusted(10.0, y - scaleRect.top() - 9.0, -4.0, 0.0);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    }
    painter.setPen(tick);
    painter.drawText(scaleRect.adjusted(10, 4, -2, -2), Qt::AlignLeft | Qt::AlignTop, priceAxis ? QStringLiteral("price") : QStringLiteral("bps"));
}

void drawTimeTicks(QPainter& painter, const QRectF& plotRect, const QRectF& timeRect, const Ranges& ranges) {
    painter.setFont(QFont{painter.font().family(), 10});
    const QColor tick{224, 224, 230};
    painter.setPen(tick);
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double f = static_cast<double>(i) / static_cast<double>(ticks);
        const qreal x = plotRect.left() + f * plotRect.width();
        const auto ts = ranges.tsMin + static_cast<std::int64_t>(f * static_cast<double>(ranges.tsMax - ranges.tsMin));
        painter.drawLine(QPointF{x, timeRect.top()}, QPointF{x, timeRect.top() + 5.0});
        const QString label = formatTimeOffset(ts, ranges);
        const QRectF labelRect{x - 42.0, timeRect.top() + 8.0, 84.0, 16.0};
        painter.drawText(labelRect, Qt::AlignCenter, label);
    }
}

const QRectF* rectForPoint(const QPointF& point, const LayoutRects& layout) noexcept {
    if (layout.primaryRect.contains(point)) return &layout.primaryRect;
    if (layout.spreadRect.contains(point)) return &layout.spreadRect;
    return nullptr;
}


void drawLabel(QPainter& painter, const QPointF& anchor, const QString& text, const QRectF& bounds) {
    if (text.isEmpty()) return;
    const QFontMetricsF fm{painter.font()};
    QRectF box{anchor.x() + 10.0, anchor.y() - 24.0, fm.horizontalAdvance(text) + 14.0, 22.0};
    if (box.right() > bounds.right()) box.moveRight(bounds.right() - 4.0);
    if (box.left() < bounds.left()) box.moveLeft(bounds.left() + 4.0);
    if (box.top() < bounds.top()) box.moveTop(anchor.y() + 10.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor{18, 18, 21, 230});
    painter.drawRect(box);
    painter.setPen(QColor{245, 245, 245});
    painter.drawText(box.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

}  // namespace

BookTickerCompareItem::BookTickerCompareItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

void BookTickerCompareItem::setController(BookTickerCompareController* controller) {
    if (controller_ == controller) return;
    if (controller_ != nullptr) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_ != nullptr) {
        connect(controller_, &BookTickerCompareController::dataChanged, this, [this]() { update(); });
        connect(controller_, &BookTickerCompareController::statusChanged, this, [this]() { update(); });
        connect(controller_, &BookTickerCompareController::viewportChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

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

void BookTickerCompareItem::paint(QPainter* painter) {
    if (painter == nullptr) return;
    painter->fillRect(boundingRect(), QColor{32, 32, 34});
    if (controller_ == nullptr || !controller_->ready()) {
        painter->setPen(QColor{170, 170, 175});
        painter->drawText(boundingRect().adjusted(16, 16, -16, -16), Qt::AlignLeft | Qt::AlignTop, controller_ ? controller_->statusText() : QStringLiteral("No comparison controller"));
        return;
    }

    const auto& primary = controller_->primaryRows();
    const auto& secondary = controller_->secondaryRows();
    const auto& spreads = controller_->spreadPoints();
    const auto& means = controller_->meanPoints();
    const Ranges ranges = computeRanges(primary, secondary, spreads, means, controller_->tsMin(), controller_->tsMax(), controller_->currentTs(), controller_->totalFeePenaltyBps());
    const LayoutRects layout = makeLayout(boundingRect());

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QColor{73, 73, 79});
    painter->drawRect(layout.primaryRect);
    painter->drawRect(layout.spreadRect);
    painter->drawRect(layout.primaryScaleRect);
    painter->drawRect(layout.spreadScaleRect);

    drawAxisTicks(*painter, layout.primaryRect, layout.primaryScaleRect, ranges, true);
    drawAxisTicks(*painter, layout.spreadRect, layout.spreadScaleRect, ranges, false);
    drawTimeTicks(*painter, layout.spreadRect, layout.timeRect, ranges);

    QPolygonF points;
    appendTickerStepPoints(primary, true, ranges, layout.primaryRect, points);
    drawPolyline(*painter, points, QColor{36, 194, 203});
    appendTickerStepPoints(primary, false, ranges, layout.primaryRect, points);
    drawPolyline(*painter, points, QColor{218, 37, 54});
    appendTickerStepPoints(secondary, true, ranges, layout.primaryRect, points);
    drawPolyline(*painter, points, QColor{76, 212, 126});
    appendTickerStepPoints(secondary, false, ranges, layout.primaryRect, points);
    drawPolyline(*painter, points, QColor{179, 102, 255});

    drawMeanBands(*painter, means, ranges, layout.spreadRect);
    drawSpreadSegments(*painter, spreads, ranges, layout.spreadRect);
    drawStrategyOverlay(*painter, controller_->strategyOverlay(), ranges, layout.primaryRect);

    painter->setFont(QFont{painter->font().family(), 9});
    painter->setPen(QColor{245, 245, 245});
    painter->drawText(layout.primaryRect.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop,
                      QStringLiteral("A: bid cyan / ask red   B: bid green / ask purple"));
    painter->drawText(layout.spreadRect.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop,
                      QStringLiteral("Spread: min %1   max %2   mean avg %3   max cost band +/- %4   max deviation %5   max edge %6")
                          .arg(formatBps(ranges.spreadMin), formatBps(ranges.spreadMax), formatBps(ranges.meanAvg), formatBps(ranges.costBandMax), formatBps(ranges.deviationAbsMax), formatBps(ranges.edgeAfterCostMax)));

    const QRectF chartBounds = boundingRect().adjusted(4, 4, -4, -4);
    if (hoverActive_ && layout.spreadRect.contains(hoverPoint_)) {
        const auto ts = tsForX(hoverPoint_.x(), ranges, layout.spreadRect);
        const auto* spread = nearestSpreadPoint(spreads, ts);
        const auto* mean = nearestMeanPoint(means, ts);
        if (spread != nullptr && mean != nullptr) {
            const QString label = QStringLiteral("spread %1  mean %2  dev %3  cost %4  edge %5")
                                      .arg(formatBps(spread->spreadBps),
                                           formatBps(mean->meanBps),
                                           formatBps(mean->deviationBps),
                                           formatBps(mean->costBandBps),
                                           formatBps(mean->edgeAfterCostBps));
            drawLabel(*painter, hoverPoint_, label, chartBounds);
        }
    }
    if (measureVisible_) {
        const QRectF* startRect = rectForPoint(measureStart_, layout);
        const QRectF* endRect = rectForPoint(measureEnd_, layout);
        if (startRect != nullptr && endRect == startRect) {
            QPen measurePen{QColor{255, 230, 120}};
            measurePen.setWidth(1);
            painter->setPen(measurePen);
            painter->drawLine(measureStart_, measureEnd_);
            painter->drawEllipse(measureStart_, 3.0, 3.0);
            painter->drawEllipse(measureEnd_, 3.0, 3.0);

            const auto startTs = tsForX(measureStart_.x(), ranges, *startRect);
            const auto endTs = tsForX(measureEnd_.x(), ranges, *startRect);
            QString label = QStringLiteral("dt %1").arg(formatDurationNs(endTs - startTs));
            if (startRect == &layout.spreadRect) {
                const double b0 = spreadForY(measureStart_.y(), ranges, layout.spreadRect);
                const double b1 = spreadForY(measureEnd_.y(), ranges, layout.spreadRect);
                label += QStringLiteral("  d %1").arg(formatBps(b1 - b0));
            } else {
                const auto p0 = priceForY(measureStart_.y(), ranges, *startRect);
                const auto p1 = priceForY(measureEnd_.y(), ranges, *startRect);
                const double delta = static_cast<double>(p1 - p0) / kE8;
                const double relBps = p0 != 0 ? (static_cast<double>(p1 - p0) / static_cast<double>(p0)) * 10000.0 : 0.0;
                label += QStringLiteral("  dP %1  %2").arg(QString::number(delta, 'f', 6), formatBps(relBps));
            }
            drawLabel(*painter, measureEnd_, label, chartBounds);
        }
    }
}

}  // namespace hftrec::gui::viewer
















