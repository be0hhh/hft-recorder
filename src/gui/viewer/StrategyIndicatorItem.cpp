#include "gui/viewer/StrategyIndicatorItem.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/StrategyIndicator.hpp"

namespace hftrec::gui::viewer {
namespace {

struct PixelBucket {
    bool has{false};
    std::int64_t minValue{0};
    std::int64_t maxValue{0};
    std::int64_t lastValue{0};
};

qreal mapY(std::int64_t value, std::int64_t minValue, std::int64_t maxValue, qreal top, qreal height) noexcept {
    if (maxValue <= minValue) return top + height * 0.5;
    const qreal ratio = static_cast<qreal>(value - minValue) / static_cast<qreal>(maxValue - minValue);
    return top + height - ratio * height;
}

qreal mapX(std::int64_t tsNs, std::int64_t tsMin, std::int64_t tsMax, qreal left, qreal width) noexcept {
    if (tsMax <= tsMin) return left;
    const qreal ratio = static_cast<qreal>(tsNs - tsMin) / static_cast<qreal>(tsMax - tsMin);
    return left + ratio * width;
}

QString compactValue(std::int64_t value, const QString& unit) {
    const qint64 absValue = value < 0 ? -value : value;
    QString suffix;
    qint64 div = 1;
    if (absValue >= 1000000000ll) { suffix = QStringLiteral("B"); div = 1000000000ll; }
    else if (absValue >= 1000000ll) { suffix = QStringLiteral("M"); div = 1000000ll; }
    else if (absValue >= 1000ll) { suffix = QStringLiteral("K"); div = 1000ll; }
    const QString base = div == 1 ? QString::number(value) : QStringLiteral("%1.%2%3").arg(value / div).arg((absValue % div) / (div / 10)).arg(suffix);
    return unit.isEmpty() ? base : QStringLiteral("%1 %2").arg(base, unit);
}

bool isToxicFlowPairIndicator(const StrategyIndicatorData& indicator) noexcept {
    return indicator.profile == QStringLiteral("toxic_flow_pair");
}

QString indicatorRawTitle(const StrategyIndicatorData& indicator) {
    QString title = indicator.title.isEmpty() ? indicator.profile : indicator.title;
    if (isToxicFlowPairIndicator(indicator) && !indicator.valueLabel.isEmpty() && !indicator.auxLabel.isEmpty()) {
        title = title.isEmpty()
            ? QStringLiteral("%1/%2").arg(indicator.valueLabel, indicator.auxLabel)
            : QStringLiteral("%1 %2/%3").arg(title, indicator.valueLabel, indicator.auxLabel);
    } else if (!indicator.auxLabel.isEmpty()) {
        title = title.isEmpty() ? indicator.auxLabel : QStringLiteral("%1 %2").arg(title, indicator.auxLabel);
    }
    return title;
}

}  // namespace

StrategyIndicatorItem::StrategyIndicatorItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void StrategyIndicatorItem::setController(ChartController* controller) {
    if (controller_ == controller) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged, this, [this]() { update(); });
        connect(controller_, &ChartController::backtestResultChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

void StrategyIndicatorItem::paint(QPainter* painter) {
    const QRectF bounds = boundingRect();
    painter->fillRect(bounds, QColor("#15171c"));
    if (!controller_ || !controller_->hasStrategyIndicator()) return;

    const StrategyIndicatorData& indicator = controller_->strategyIndicator();
    const std::int64_t tsMin = controller_->tsMin();
    const std::int64_t tsMax = controller_->tsMax();
    if (indicator.points.empty() || tsMax <= tsMin) return;

    const qreal left = 0.0;
    const qreal rightScale = 84.0;
    const qreal top = 8.0;
    const qreal bottom = 18.0;
    const qreal plotWidth = std::max<qreal>(1.0, bounds.width() - rightScale);
    const qreal plotHeight = std::max<qreal>(1.0, bounds.height() - top - bottom);

    std::int64_t minValue = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxValue = std::numeric_limits<std::int64_t>::min();
    std::size_t firstVisible = indicator.points.size();
    std::size_t lastVisible = 0;
    const bool pairIndicator = isToxicFlowPairIndicator(indicator);
    for (std::size_t i = 0; i < indicator.points.size(); ++i) {
        const auto& point = indicator.points[i];
        if (point.tsNs < tsMin || point.tsNs > tsMax) continue;
        if (firstVisible == indicator.points.size()) firstVisible = i;
        lastVisible = i;
        if (pairIndicator) {
            minValue = std::min(minValue, point.valueRaw);
            maxValue = std::max(maxValue, point.valueRaw);
        }
        minValue = std::min(minValue, point.auxRaw);
        maxValue = std::max(maxValue, point.auxRaw);
    }
    if (firstVisible == indicator.points.size()) return;
    if (minValue == maxValue) {
        minValue -= 1;
        maxValue += 1;
    }

    painter->setPen(QPen(QColor("#2b303a"), 1));
    for (int i = 0; i <= 3; ++i) {
        const qreal y = top + plotHeight * static_cast<qreal>(i) / 3.0;
        painter->drawLine(QPointF(left, y), QPointF(left + plotWidth, y));
    }
    painter->drawLine(QPointF(left + plotWidth, top), QPointF(left + plotWidth, top + plotHeight));

    const int bucketCount = std::max(1, static_cast<int>(std::ceil(plotWidth)));
    std::vector<PixelBucket> valueBuckets(static_cast<std::size_t>(pairIndicator ? bucketCount : 0));
    std::vector<PixelBucket> buckets(static_cast<std::size_t>(bucketCount));
    auto absorbBucket = [](PixelBucket& bucket, std::int64_t value) noexcept {
        if (!bucket.has) {
            bucket.has = true;
            bucket.minValue = value;
            bucket.maxValue = value;
        } else {
            bucket.minValue = std::min(bucket.minValue, value);
            bucket.maxValue = std::max(bucket.maxValue, value);
        }
        bucket.lastValue = value;
    };
    for (std::size_t i = firstVisible; i <= lastVisible; ++i) {
        const auto& point = indicator.points[i];
        if (point.tsNs < tsMin || point.tsNs > tsMax) continue;
        int x = static_cast<int>(std::clamp<qreal>(mapX(point.tsNs, tsMin, tsMax, left, plotWidth), 0.0, plotWidth - 1.0));
        if (pairIndicator) absorbBucket(valueBuckets[static_cast<std::size_t>(x)], point.valueRaw);
        absorbBucket(buckets[static_cast<std::size_t>(x)], point.auxRaw);
    }

    auto drawBuckets = [&](const std::vector<PixelBucket>& source, const QColor& color, qreal width) {
        painter->setPen(QPen(color, width));
        QPainterPath path;
        bool started = false;
        QPointF prev;
        for (std::size_t x = 0; x < source.size(); ++x) {
            const PixelBucket& bucket = source[x];
            if (!bucket.has) continue;
            const qreal px = left + static_cast<qreal>(x);
            const qreal yMin = mapY(bucket.minValue, minValue, maxValue, top, plotHeight);
            const qreal yMax = mapY(bucket.maxValue, minValue, maxValue, top, plotHeight);
            if (std::abs(yMin - yMax) > 1.0) painter->drawLine(QPointF(px, yMin), QPointF(px, yMax));
            const QPointF current(px, mapY(bucket.lastValue, minValue, maxValue, top, plotHeight));
            if (!started) {
                path.moveTo(current);
                started = true;
            } else if (current.x() > prev.x()) {
                path.lineTo(current);
            }
            prev = current;
        }
        if (started) painter->drawPath(path);
    };

    if (pairIndicator) drawBuckets(valueBuckets, QColor("#58c8ff"), 1.6);
    drawBuckets(buckets, pairIndicator ? QColor("#ff5c70") : QColor("#24c2cb"), 1.6);

    painter->setPen(QColor("#f1f4f8"));
    painter->setFont(QFont(painter->font().family(), 10));
    const QString title = indicatorRawTitle(indicator);
    painter->drawText(QRectF(10, 4, plotWidth - 20, 18), Qt::AlignLeft | Qt::AlignVCenter, title);

    painter->setPen(QColor("#a8afbd"));
    painter->setFont(QFont(painter->font().family(), 9));
    painter->drawText(QRectF(plotWidth + 6, top - 2, rightScale - 10, 16), Qt::AlignRight | Qt::AlignVCenter, compactValue(maxValue, indicator.unit));
    painter->drawText(QRectF(plotWidth + 6, top + plotHeight - 14, rightScale - 10, 16), Qt::AlignRight | Qt::AlignVCenter, compactValue(minValue, indicator.unit));
}

}  // namespace hftrec::gui::viewer
