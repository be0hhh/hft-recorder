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
    std::int64_t priceMin{0};
    std::int64_t priceMax{0};
    double spreadMin{0.0};
    double spreadMax{1.0};
    double rawSpreadMax{0.0};
    double internalPenaltyAvg{0.0};
    double feePenaltyAvg{0.0};
    double meanAvg{0.0};
    double deviationAbsMax{0.0};
    double edgeAfterFeesMax{0.0};
    double totalFeesBps{0.0};
};

struct LayoutRects {
    QRectF priceRect{};
    QRectF spreadRect{};
    QRectF timeRect{};
    QRectF priceScaleRect{};
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
                     double totalFeesBps) noexcept {
    Ranges ranges{};
    ranges.tsMin = tsMin;
    ranges.tsMax = tsMax > tsMin ? tsMax : tsMin + 1;
    ranges.totalFeesBps = totalFeesBps;
    bool hasPrice = false;
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
            ranges.spreadMin = std::min(point.meanBps - totalFeesBps, point.meanBps);
            ranges.spreadMax = std::max(point.meanBps + totalFeesBps, point.meanBps);
            hasSpread = true;
        } else {
            ranges.spreadMin = std::min(ranges.spreadMin, point.meanBps - totalFeesBps);
            ranges.spreadMax = std::max(ranges.spreadMax, point.meanBps + totalFeesBps);
        }
        meanSum += point.meanBps;
        ranges.deviationAbsMax = std::max(ranges.deviationAbsMax, std::abs(point.deviationBps));
        ranges.edgeAfterFeesMax = std::max(ranges.edgeAfterFeesMax, point.edgeAfterFeesBps);
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
    const qreal priceHeight = (plotBottom - full.top()) * 0.68 - kGap * 0.5;
    LayoutRects layout{};
    layout.priceRect = QRectF{full.left(), full.top(), plotRight - full.left(), priceHeight};
    layout.spreadRect = QRectF{full.left(), layout.priceRect.bottom() + kGap, plotRight - full.left(), plotBottom - layout.priceRect.bottom() - kGap};
    layout.timeRect = QRectF{layout.priceRect.left(), plotBottom, layout.priceRect.width(), kBottomScaleHeight};
    layout.priceScaleRect = QRectF{plotRight, layout.priceRect.top(), kRightScaleWidth, layout.priceRect.height()};
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

void appendTickerPoints(const std::vector<hftrec::replay::BookTickerRow>& rows,
                        bool bid,
                        const Ranges& ranges,
                        const QRectF& rect,
                        QPolygonF& out) {
    out.clear();
    out.reserve(static_cast<int>(std::min<std::size_t>(rows.size() + 2, 1000000)));

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
    for (const auto& row : rows) {
        if (row.tsNs < ranges.tsMin) continue;
        if (row.tsNs > ranges.tsMax) break;
        const auto price = bid ? row.bidPriceE8 : row.askPriceE8;
        if (price <= 0) continue;
        out.push_back(QPointF{xFor(row.tsNs, ranges, rect), priceYFor(price, ranges, rect)});
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
            drawSegment(prev, current, direction);
        }
        prev = current;
        prevDirection = point.direction;
        havePrev = true;
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
    auto append = [&](std::int64_t ts, double mean) {
        const double x = xFor(ts, ranges, rect);
        meanLine.push_back(QPointF{x, spreadYFor(mean, ranges, rect)});
        upperBand.push_back(QPointF{x, spreadYFor(mean + ranges.totalFeesBps, ranges, rect)});
        lowerBand.push_back(QPointF{x, spreadYFor(mean - ranges.totalFeesBps, ranges, rect)});
    };
    if (carry != nullptr) append(ranges.tsMin, carry->meanBps);
    for (const auto& point : means) {
        if (point.tsNs < ranges.tsMin) continue;
        if (point.tsNs > ranges.tsMax) break;
        append(point.tsNs, point.meanBps);
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
    if (layout.priceRect.contains(point)) return &layout.priceRect;
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
    const Ranges ranges = computeRanges(primary, secondary, spreads, means, controller_->tsMin(), controller_->tsMax(), controller_->totalFeePenaltyBps());
    const LayoutRects layout = makeLayout(boundingRect());

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QColor{73, 73, 79});
    painter->drawRect(layout.priceRect);
    painter->drawRect(layout.spreadRect);
    painter->drawRect(layout.priceScaleRect);
    painter->drawRect(layout.spreadScaleRect);

    drawAxisTicks(*painter, layout.priceRect, layout.priceScaleRect, ranges, true);
    drawAxisTicks(*painter, layout.spreadRect, layout.spreadScaleRect, ranges, false);
    drawTimeTicks(*painter, layout.priceRect, layout.timeRect, ranges);

    const double zeroY = spreadYFor(0.0, ranges, layout.spreadRect);
    QPen zeroPen{QColor{185, 185, 192}};
    zeroPen.setWidth(1);
    zeroPen.setStyle(Qt::DashLine);
    painter->setPen(zeroPen);
    painter->drawLine(QPointF{layout.spreadRect.left(), zeroY}, QPointF{layout.spreadRect.right(), zeroY});

    QPolygonF points;
    appendTickerPoints(primary, true, ranges, layout.priceRect, points);
    drawPolyline(*painter, points, QColor{36, 194, 203});
    appendTickerPoints(primary, false, ranges, layout.priceRect, points);
    drawPolyline(*painter, points, QColor{218, 37, 54});
    appendTickerPoints(secondary, true, ranges, layout.priceRect, points);
    drawPolyline(*painter, points, QColor{76, 212, 126});
    appendTickerPoints(secondary, false, ranges, layout.priceRect, points);
    drawPolyline(*painter, points, QColor{179, 102, 255});

    drawMeanBands(*painter, means, ranges, layout.spreadRect);
    drawSpreadSegments(*painter, spreads, ranges, layout.spreadRect);

    painter->setFont(QFont{painter->font().family(), 9});
    painter->setPen(QColor{245, 245, 245});
    painter->drawText(layout.priceRect.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop,
                      QStringLiteral("Top: A bid cyan / A ask red   B bid green / B ask purple"));
    painter->drawText(layout.spreadRect.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop,
                      QStringLiteral("Basis: spread min %1   spread max %2   mean avg %3   fees band +/- %4   max deviation %5   max edge %6")
                          .arg(formatBps(ranges.spreadMin), formatBps(ranges.spreadMax), formatBps(ranges.meanAvg), formatBps(ranges.totalFeesBps), formatBps(ranges.deviationAbsMax), formatBps(ranges.edgeAfterFeesMax)));

    const QRectF chartBounds = boundingRect().adjusted(4, 4, -4, -4);
    if (hoverActive_ && layout.spreadRect.contains(hoverPoint_)) {
        const auto ts = tsForX(hoverPoint_.x(), ranges, layout.spreadRect);
        const auto* spread = nearestSpreadPoint(spreads, ts);
        const auto* mean = nearestMeanPoint(means, ts);
        if (spread != nullptr && mean != nullptr) {
            const QString label = QStringLiteral("spread %1  mean %2  dev %3  fees %4  edge %5")
                                      .arg(formatBps(spread->spreadBps),
                                           formatBps(mean->meanBps),
                                           formatBps(mean->deviationBps),
                                           formatBps(ranges.totalFeesBps),
                                           formatBps(mean->edgeAfterFeesBps));
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
            if (startRect == &layout.priceRect) {
                const auto p0 = priceForY(measureStart_.y(), ranges, layout.priceRect);
                const auto p1 = priceForY(measureEnd_.y(), ranges, layout.priceRect);
                const double delta = static_cast<double>(p1 - p0) / kE8;
                const double relBps = p0 != 0 ? (static_cast<double>(p1 - p0) / static_cast<double>(p0)) * 10000.0 : 0.0;
                label += QStringLiteral("  dP %1  %2").arg(QString::number(delta, 'f', 6), formatBps(relBps));
            } else {
                const double b0 = spreadForY(measureStart_.y(), ranges, layout.spreadRect);
                const double b1 = spreadForY(measureEnd_.y(), ranges, layout.spreadRect);
                label += QStringLiteral("  d %1").arg(formatBps(b1 - b0));
            }
            drawLabel(*painter, measureEnd_, label, chartBounds);
        }
    }
}

}  // namespace hftrec::gui::viewer
















