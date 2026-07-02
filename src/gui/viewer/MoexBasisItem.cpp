#include "gui/viewer/MoexBasisItem.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/arbitrage/PriceBasis.hpp"
#include "gui/viewer/BookTickerCompareCandlePaint.hpp"
#include "gui/viewer/MoexBasisController.hpp"

namespace hftrec::gui::viewer {
namespace {

struct Range {
    double min{0.0};
    double max{1.0};
};

std::array<QColor, 10> palette() {
    return {
        QColor{36, 194, 203},
        QColor{239, 111, 108},
        QColor{235, 196, 84},
        QColor{126, 190, 112},
        QColor{177, 132, 255},
        QColor{244, 142, 82},
        QColor{116, 165, 255},
        QColor{216, 116, 181},
        QColor{158, 210, 132},
        QColor{220, 220, 226},
    };
}

double xFor(std::int64_t ts, qint64 tsMin, qint64 tsMax, const QRectF& rect) noexcept {
    return rect.left() + (static_cast<double>(ts - tsMin) / static_cast<double>(tsMax - tsMin)) * rect.width();
}

double yFor(double value, Range range, const QRectF& rect) noexcept {
    return rect.bottom() - ((value - range.min) / (range.max - range.min)) * rect.height();
}

Range padded(Range range) noexcept {
    if (range.max <= range.min) range.max = range.min + 1.0;
    const double pad = std::max((range.max - range.min) * 0.08, 1.0);
    return Range{range.min - pad, range.max + pad};
}

Range applyScale(Range range, double zoom, double pan) noexcept {
    range = padded(range);
    zoom = std::max(1.0, zoom);
    const double baseSpan = range.max - range.min;
    const double span = baseSpan / zoom;
    const double center = (range.min + range.max) * 0.5 + pan * baseSpan;
    return Range{center - span * 0.5, center + span * 0.5};
}

bool absorb(Range& range, bool& hasValue, double value) noexcept {
    if (!std::isfinite(value)) return false;
    if (!hasValue) {
        range.min = value;
        range.max = value;
        hasValue = true;
        return true;
    }
    if (value < range.min) range.min = value;
    if (value > range.max) range.max = value;
    return true;
}

std::int64_t normalizedClose(const hftrec::replay::CandleRow& row, std::int64_t priceBasisQtyE8) noexcept {
    return hftrec::arbitrage::normalizeNativePriceE8(moexBasisClosePriceE8(row), priceBasisQtyE8);
}

std::int64_t normalizedPrice(std::int64_t priceE8, std::int64_t priceBasisQtyE8) noexcept {
    return hftrec::arbitrage::normalizeNativePriceE8(priceE8, priceBasisQtyE8 <= 0 ? 100000000LL : priceBasisQtyE8);
}

Range priceRange(const MoexBasisController& controller, qint64 tsMin, qint64 tsMax) {
    Range range{};
    bool hasValue = false;
    for (const auto& row : controller.spotLeg().candles) {
        if (row.tsNs < tsMin || row.tsNs > tsMax) continue;
        if (row.hasOhlc && row.highE8 > 0 && row.lowE8 > 0) {
            absorb(range, hasValue, static_cast<double>(row.highE8));
            absorb(range, hasValue, static_cast<double>(row.lowE8));
        } else {
            absorb(range, hasValue, static_cast<double>(moexBasisClosePriceE8(row)));
        }
    }
    for (const auto& future : controller.renderFutureLegs()) {
        if (!future.enabled || !future.metadataReady) continue;
        for (const auto& row : future.candles) {
            if (row.tsNs < tsMin || row.tsNs > tsMax) continue;
            if (row.hasOhlc && row.highE8 > 0 && row.lowE8 > 0) {
                absorb(range, hasValue, static_cast<double>(normalizedPrice(row.highE8, future.priceBasisQtyE8)));
                absorb(range, hasValue, static_cast<double>(normalizedPrice(row.lowE8, future.priceBasisQtyE8)));
            } else {
                absorb(range, hasValue, static_cast<double>(normalizedClose(row, future.priceBasisQtyE8)));
            }
        }
    }
    if (!hasValue) return Range{0.0, 1.0};
    return applyScale(range, controller.priceZoom(), controller.pricePan());
}

Range basisRange(const MoexBasisController& controller, qint64 tsMin, qint64 tsMax) {
    Range range{};
    bool hasValue = false;
    absorb(range, hasValue, 0.0);
    for (const auto& future : controller.renderFutureLegs()) {
        if (!future.enabled) continue;
        for (const auto& point : future.basisPoints) {
            if (point.tsNs < tsMin || point.tsNs > tsMax) continue;
            absorb(range, hasValue, point.basisBps);
        }
    }
    for (const auto& point : controller.strategyOverlay().spreadPoints) {
        if (point.tsNs < tsMin || point.tsNs > tsMax) continue;
        const double spread = static_cast<double>(point.spreadBpsE8) / 100000000.0;
        const double ema = static_cast<double>(point.emaBpsE8) / 100000000.0;
        const double cost = static_cast<double>(point.costBandBpsE8) / 100000000.0;
        absorb(range, hasValue, spread);
        absorb(range, hasValue, ema + cost);
        absorb(range, hasValue, ema - cost);
    }
    return applyScale(range, controller.basisZoom(), controller.basisPan());
}

void drawGrid(QPainter& painter, const QRectF& rect, const QColor& color) {
    QPen pen{color};
    pen.setWidth(1);
    painter.setPen(pen);
    for (int i = 1; i < 5; ++i) {
        const double y = rect.top() + rect.height() * (static_cast<double>(i) / 5.0);
        painter.drawLine(QPointF{rect.left(), y}, QPointF{rect.right(), y});
    }
    for (int i = 1; i < 6; ++i) {
        const double x = rect.left() + rect.width() * (static_cast<double>(i) / 6.0);
        painter.drawLine(QPointF{x, rect.top()}, QPointF{x, rect.bottom()});
    }
}

void drawBasisLine(QPainter& painter,
                   const std::vector<MoexBasisPoint>& points,
                   qint64 tsMin,
                   qint64 tsMax,
                   Range range,
                   const QRectF& rect,
                   const QColor& color) {
    QPen pen{color};
    pen.setWidth(2);
    pen.setCapStyle(Qt::SquareCap);
    painter.setPen(pen);
    bool hasPrevious = false;
    QPointF previous{};
    for (const auto& point : points) {
        if (point.tsNs < tsMin) continue;
        if (point.tsNs > tsMax) break;
        const QPointF current{xFor(point.tsNs, tsMin, tsMax, rect), yFor(point.basisBps, range, rect)};
        if (hasPrevious) painter.drawLine(previous, current);
        previous = current;
        hasPrevious = true;
    }
}

std::int64_t candleDurationNs(const hftrec::replay::CandleRow& row) noexcept {
    if (row.durationNs > 0) return row.durationNs;
    if (row.tier == 1) return 60ll * 1000000000ll;
    if (row.tier == 2) return 15ll * 60ll * 1000000000ll;
    if (row.tier == 3) return 24ll * 60ll * 60ll * 1000000000ll;
    return 60ll * 1000000000ll;
}

std::int64_t rangeBound(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    if (value > static_cast<double>(std::numeric_limits<std::int64_t>::max())) return std::numeric_limits<std::int64_t>::max();
    if (value < static_cast<double>(std::numeric_limits<std::int64_t>::min())) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(std::llround(value));
}

BookTickerCompareCandlePaintRanges paintRanges(qint64 tsMin, qint64 tsMax, Range prices, Range basis) noexcept {
    BookTickerCompareCandlePaintRanges ranges;
    ranges.tsMin = static_cast<std::int64_t>(tsMin);
    ranges.tsMax = static_cast<std::int64_t>(tsMax);
    ranges.priceMin = rangeBound(prices.min);
    ranges.priceMax = rangeBound(prices.max <= prices.min ? prices.min + 1.0 : prices.max);
    ranges.spreadMin = basis.min;
    ranges.spreadMax = basis.max <= basis.min ? basis.min + 1.0 : basis.max;
    return ranges;
}

void drawNormalizedCandleBodies(QPainter& painter,
                                const std::vector<hftrec::replay::CandleRow>& rows,
                                const BookTickerCompareCandlePaintRanges& ranges,
                                const QRectF& rect,
                                const QColor& sourceFillColor,
                                std::int64_t priceBasisQtyE8) {
    const Range priceRange{static_cast<double>(ranges.priceMin), static_cast<double>(ranges.priceMax)};
    for (const auto& row : rows) {
        if (row.tsNs < ranges.tsMin) continue;
        if (row.tsNs > ranges.tsMax) break;
        if (!row.hasOhlc) continue;

        const std::int64_t open = normalizedPrice(row.openE8, priceBasisQtyE8);
        const std::int64_t high = normalizedPrice(row.highE8, priceBasisQtyE8);
        const std::int64_t low = normalizedPrice(row.lowE8, priceBasisQtyE8);
        const std::int64_t close = normalizedPrice(moexBasisClosePriceE8(row), priceBasisQtyE8);
        if (open <= 0 || high <= 0 || low <= 0 || close <= 0 || high < low) continue;

        const qreal x = xFor(row.tsNs, ranges.tsMin, ranges.tsMax, rect);
        const qreal nextX = xFor(std::min<std::int64_t>(row.tsNs + candleDurationNs(row), ranges.tsMax),
                                 ranges.tsMin,
                                 ranges.tsMax,
                                 rect);
        const qreal width = std::clamp(std::abs(nextX - x) * 0.65, 3.0, 14.0);
        if ((x + width) < rect.left() || (x - width) > rect.right()) continue;

        const qreal yHigh = yFor(static_cast<double>(high), priceRange, rect);
        const qreal yLow = yFor(static_cast<double>(low), priceRange, rect);
        const qreal yOpen = yFor(static_cast<double>(open), priceRange, rect);
        const qreal yClose = yFor(static_cast<double>(close), priceRange, rect);

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

double bpsFromE8(std::int64_t value) noexcept {
    return static_cast<double>(value) / 100000000.0;
}

QColor strategyDirectionColor(std::uint8_t direction) {
    if (direction == 1u) return QColor{36, 194, 203};
    if (direction == 2u) return QColor{239, 111, 108};
    return QColor{235, 235, 240};
}

double spreadYFor(double spreadBps, const BookTickerCompareCandlePaintRanges& ranges, const QRectF& rect) noexcept {
    return rect.bottom() - ((spreadBps - ranges.spreadMin) / (ranges.spreadMax - ranges.spreadMin)) * rect.height();
}

void drawPolyline(QPainter& painter, const QPolygonF& line, const QColor& color, int width, Qt::PenStyle style = Qt::SolidLine) {
    if (line.size() < 2) return;
    QPen pen{color};
    pen.setWidth(width);
    pen.setStyle(style);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.drawPolyline(line);
}

void drawStrategySpread(QPainter& painter,
                        const std::vector<StrategySpreadPoint>& points,
                        const BookTickerCompareCandlePaintRanges& ranges,
                        const QRectF& rect) {
    if (points.empty()) return;
    QPolygonF emaLine;
    QPolygonF upperBand;
    QPolygonF lowerBand;
    QPointF previousSpread;
    std::uint8_t previousDirection = 0u;
    bool hasPreviousSpread = false;

    for (const auto& point : points) {
        if (point.tsNs < ranges.tsMin) continue;
        if (point.tsNs > ranges.tsMax) break;
        const double x = xFor(point.tsNs, ranges.tsMin, ranges.tsMax, rect);
        const double spread = bpsFromE8(point.spreadBpsE8);
        const double ema = bpsFromE8(point.emaBpsE8);
        const double cost = bpsFromE8(point.costBandBpsE8);
        const QPointF current{x, spreadYFor(spread, ranges, rect)};
        if (hasPreviousSpread) {
            QPen pen{strategyDirectionColor(point.direction != 0u ? point.direction : previousDirection)};
            pen.setWidth(2);
            pen.setCapStyle(Qt::SquareCap);
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            painter.drawLine(previousSpread, current);
        }
        previousSpread = current;
        previousDirection = point.direction;
        hasPreviousSpread = true;
        emaLine.push_back(QPointF{x, spreadYFor(ema, ranges, rect)});
        upperBand.push_back(QPointF{x, spreadYFor(ema + cost, ranges, rect)});
        lowerBand.push_back(QPointF{x, spreadYFor(ema - cost, ranges, rect)});
    }

    drawPolyline(painter, upperBand, QColor{145, 145, 150, 160}, 1, Qt::DashLine);
    drawPolyline(painter, lowerBand, QColor{145, 145, 150, 160}, 1, Qt::DashLine);
    drawPolyline(painter, emaLine, QColor{255, 214, 84}, 2);
}

}  // namespace

MoexBasisItem::MoexBasisItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::AllButtons);
}

void MoexBasisItem::setController(MoexBasisController* controller) {
    if (controller_ == controller) return;
    if (controller_ != nullptr) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_ != nullptr) {
        connect(controller_, &MoexBasisController::dataChanged, this, [this]() { update(); });
        connect(controller_, &MoexBasisController::viewportChanged, this, [this]() { update(); });
        connect(controller_, &MoexBasisController::statusChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

void MoexBasisItem::setHoverPoint(qreal x, qreal y) {
    hoverActive_ = true;
    hoverPoint_ = QPointF{x, y};
    update();
}

void MoexBasisItem::clearHover() {
    hoverActive_ = false;
    update();
}

bool MoexBasisItem::isPricePanelPoint(qreal, qreal y) const {
    return y < height() * 0.64;
}

bool MoexBasisItem::isBasisPanelPoint(qreal, qreal y) const {
    return y >= height() * 0.64;
}

double MoexBasisItem::priceAnchorFraction(qreal y) const {
    const double panelBottom = std::max<qreal>(1, height() * 0.64);
    return std::clamp(static_cast<double>(y / panelBottom), 0.0, 1.0);
}

double MoexBasisItem::basisAnchorFraction(qreal y) const {
    const double top = height() * 0.64;
    const double span = std::max<qreal>(1, height() - top);
    return std::clamp(static_cast<double>((y - top) / span), 0.0, 1.0);
}

void MoexBasisItem::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), QColor{32, 32, 34});
    if (controller_ == nullptr) return;

    const QRectF all = boundingRect().adjusted(10, 8, -10, -8);
    const QRectF priceRect{all.left(), all.top(), all.width(), all.height() * 0.62};
    const QRectF basisRect{all.left(), priceRect.bottom() + 10, all.width(), all.bottom() - priceRect.bottom() - 10};
    painter->fillRect(priceRect, QColor{26, 26, 28});
    painter->fillRect(basisRect, QColor{26, 26, 28});
    drawGrid(*painter, priceRect, QColor{72, 72, 78, 80});
    drawGrid(*painter, basisRect, QColor{72, 72, 78, 80});

    const qint64 tsMin = controller_->tsMin();
    const qint64 tsMax = controller_->tsMax();
    if (tsMax <= tsMin || controller_->spotLeg().candles.empty()) {
        painter->setPen(QColor{170, 170, 176});
        painter->drawText(all, Qt::AlignCenter, controller_->statusText());
        return;
    }

    const Range prices = priceRange(*controller_, tsMin, tsMax);
    const Range basis = basisRange(*controller_, tsMin, tsMax);
    const BookTickerCompareCandlePaintRanges ranges = paintRanges(tsMin, tsMax, prices, basis);
    const auto colors = palette();

    drawCompareCandleBodies(*painter,
                            controller_->spotLeg().candles,
                            ranges,
                            priceRect,
                            QColor{228, 228, 232});

    int colorIndex = 0;
    for (const auto& future : controller_->renderFutureLegs()) {
        if (!future.enabled || !future.metadataReady) continue;
        const QColor color = colors[static_cast<std::size_t>(colorIndex) % colors.size()];
        ++colorIndex;
        drawNormalizedCandleBodies(*painter, future.candles, ranges, priceRect, color, future.priceBasisQtyE8);
        drawBasisLine(*painter, future.basisPoints, tsMin, tsMax, basis, basisRect, color);
    }
    drawStrategySpread(*painter, controller_->strategyOverlay().spreadPoints, ranges, basisRect);

    QPen zeroPen{QColor{230, 230, 235, 115}};
    zeroPen.setWidth(1);
    painter->setPen(zeroPen);
    const double zeroY = yFor(0.0, basis, basisRect);
    painter->drawLine(QPointF{basisRect.left(), zeroY}, QPointF{basisRect.right(), zeroY});

    painter->setPen(QColor{235, 235, 240});
    painter->drawText(QRectF{priceRect.left() + 8, priceRect.top() + 6, 320, 20}, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spot + futures candles"));
    painter->drawText(QRectF{basisRect.left() + 8, basisRect.top() + 6, 320, 20}, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Basis spread bps"));

    if (hoverActive_) {
        QPen hoverPen{QColor{245, 245, 245, 120}};
        hoverPen.setWidth(1);
        painter->setPen(hoverPen);
        painter->drawLine(QPointF{hoverPoint_.x(), all.top()}, QPointF{hoverPoint_.x(), all.bottom()});
    }
}

}  // namespace hftrec::gui::viewer
