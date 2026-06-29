#include "gui/viewer/MoexBasisItem.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cmath>

#include "core/arbitrage/PriceBasis.hpp"
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

Range priceRange(const MoexBasisController& controller, qint64 tsMin, qint64 tsMax) {
    Range range{};
    bool hasValue = false;
    for (const auto& row : controller.spotLeg().candles) {
        if (row.tsNs < tsMin || row.tsNs > tsMax) continue;
        absorb(range, hasValue, static_cast<double>(moexBasisClosePriceE8(row)));
    }
    for (const auto& future : controller.renderFutureLegs()) {
        if (!future.enabled || !future.metadataReady) continue;
        for (const auto& row : future.candles) {
            if (row.tsNs < tsMin || row.tsNs > tsMax) continue;
            absorb(range, hasValue, static_cast<double>(normalizedClose(row, future.priceBasisQtyE8)));
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

void drawSpotCandles(QPainter& painter,
                     const std::vector<hftrec::replay::CandleRow>& candles,
                     qint64 tsMin,
                     qint64 tsMax,
                     Range range,
                     const QRectF& rect) {
    QPen wickPen{QColor{228, 228, 232, 210}};
    wickPen.setWidth(1);
    QColor fill{228, 228, 232, 82};
    painter.setPen(wickPen);
    painter.setBrush(fill);
    for (const auto& row : candles) {
        if (row.tsNs < tsMin || row.tsNs > tsMax) continue;
        if (!row.hasOhlc || row.highE8 <= 0 || row.lowE8 <= 0 || row.openE8 <= 0 || row.closeE8 <= 0) continue;
        const double x = xFor(row.tsNs, tsMin, tsMax, rect);
        const double yHigh = yFor(static_cast<double>(row.highE8), range, rect);
        const double yLow = yFor(static_cast<double>(row.lowE8), range, rect);
        const double yOpen = yFor(static_cast<double>(row.openE8), range, rect);
        const double yClose = yFor(static_cast<double>(row.closeE8), range, rect);
        const double width = std::clamp(rect.width() / 260.0, 2.0, 8.0);
        painter.drawLine(QPointF{x, yHigh}, QPointF{x, yLow});
        painter.drawRect(QRectF{x - width * 0.5, std::min(yOpen, yClose), width, std::max(2.0, std::abs(yClose - yOpen))});
    }
    painter.setBrush(Qt::NoBrush);
}

template <typename ValueFn>
void drawLine(QPainter& painter,
              const std::vector<hftrec::replay::CandleRow>& candles,
              qint64 tsMin,
              qint64 tsMax,
              Range range,
              const QRectF& rect,
              const QColor& color,
              ValueFn valueFn) {
    QPen pen{color};
    pen.setWidth(2);
    pen.setCapStyle(Qt::SquareCap);
    painter.setPen(pen);
    bool hasPrevious = false;
    QPointF previous{};
    for (const auto& row : candles) {
        if (row.tsNs < tsMin) continue;
        if (row.tsNs > tsMax) break;
        const double value = valueFn(row);
        if (value <= 0.0) {
            hasPrevious = false;
            continue;
        }
        const QPointF current{xFor(row.tsNs, tsMin, tsMax, rect), yFor(value, range, rect)};
        if (hasPrevious) painter.drawLine(previous, current);
        previous = current;
        hasPrevious = true;
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
    const auto colors = palette();

    drawSpotCandles(*painter, controller_->spotLeg().candles, tsMin, tsMax, prices, priceRect);

    int colorIndex = 0;
    for (const auto& future : controller_->renderFutureLegs()) {
        if (!future.enabled || !future.metadataReady) continue;
        const QColor color = colors[static_cast<std::size_t>(colorIndex) % colors.size()];
        ++colorIndex;
        drawLine(*painter,
                 future.candles,
                 tsMin,
                 tsMax,
                 prices,
                 priceRect,
                 color,
                 [&](const hftrec::replay::CandleRow& row) {
                     return static_cast<double>(normalizedClose(row, future.priceBasisQtyE8));
                 });
        drawBasisLine(*painter, future.basisPoints, tsMin, tsMax, basis, basisRect, color);

        if (!future.basisPoints.empty() && future.expiryUtcNs > tsMin && future.expiryUtcNs <= tsMax) {
            const double x = xFor(future.expiryUtcNs, tsMin, tsMax, priceRect);
            QColor marker{210, 74, 92, 170};
            QPen markerPen{marker};
            markerPen.setWidth(1);
            painter->setPen(markerPen);
            painter->drawLine(QPointF{x, priceRect.top()}, QPointF{x, basisRect.bottom()});
            painter->drawText(QRectF{x + 3, priceRect.top() + 4, 92, 18}, Qt::AlignLeft | Qt::AlignVCenter, future.symbol);

            const auto first = std::find_if(future.basisPoints.begin(), future.basisPoints.end(), [&](const auto& point) {
                return point.tsNs >= tsMin && point.tsNs <= tsMax;
            });
            if (first != future.basisPoints.end()) {
                QColor fair = marker;
                fair.setAlpha(105);
                QPen fairPen{fair};
                fairPen.setWidth(1);
                fairPen.setStyle(Qt::DashLine);
                painter->setPen(fairPen);
                painter->drawLine(QPointF{xFor(first->tsNs, tsMin, tsMax, basisRect), yFor(first->basisBps, basis, basisRect)},
                                  QPointF{xFor(future.expiryUtcNs, tsMin, tsMax, basisRect), yFor(0.0, basis, basisRect)});
            }
        }
        for (const auto& marker : future.rollMarkers) {
            if (marker.tsNs <= tsMin || marker.tsNs > tsMax) continue;
            const double x = xFor(marker.tsNs, tsMin, tsMax, priceRect);
            QColor rollColor{36, 194, 203, 165};
            QPen rollPen{rollColor};
            rollPen.setWidth(1);
            rollPen.setStyle(Qt::DashLine);
            painter->setPen(rollPen);
            painter->drawLine(QPointF{x, priceRect.top()}, QPointF{x, basisRect.bottom()});
            painter->drawText(QRectF{x + 3, priceRect.bottom() - 22, 100, 18},
                              Qt::AlignLeft | Qt::AlignVCenter,
                              QStringLiteral("roll %1").arg(marker.label));
        }
    }

    QPen zeroPen{QColor{230, 230, 235, 115}};
    zeroPen.setWidth(1);
    painter->setPen(zeroPen);
    const double zeroY = yFor(0.0, basis, basisRect);
    painter->drawLine(QPointF{basisRect.left(), zeroY}, QPointF{basisRect.right(), zeroY});

    painter->setPen(QColor{235, 235, 240});
    painter->drawText(QRectF{priceRect.left() + 8, priceRect.top() + 6, 260, 20}, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spot + futures candles"));
    painter->drawText(QRectF{basisRect.left() + 8, basisRect.top() + 6, 260, 20}, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Basis bps"));

    if (hoverActive_) {
        QPen hoverPen{QColor{245, 245, 245, 120}};
        hoverPen.setWidth(1);
        painter->setPen(hoverPen);
        painter->drawLine(QPointF{hoverPoint_.x(), all.top()}, QPointF{hoverPoint_.x(), all.bottom()});
    }
}

}  // namespace hftrec::gui::viewer
