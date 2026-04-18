#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <QDateTime>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QStringList>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ColorScheme.hpp"

namespace hftrec::gui::viewer {

namespace {

struct ViewportMap {
    qint64 tMin{0};
    qint64 tMax{1};
    qint64 pMin{0};
    qint64 pMax{1};
    double w{0.0};
    double h{0.0};

    double toX(qint64 ts) const noexcept {
        const double span = static_cast<double>(tMax - tMin);
        if (span <= 0.0) return 0.0;
        return static_cast<double>(ts - tMin) * w / span;
    }

    double toY(qint64 price) const noexcept {
        const double span = static_cast<double>(pMax - pMin);
        if (span <= 0.0) return 0.0;
        return h - static_cast<double>(price - pMin) * h / span;
    }
};

QString formatScaledE8(std::int64_t value) {
    const bool negative = value < 0;
    const std::uint64_t absValue = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1u
        : static_cast<std::uint64_t>(value);
    const std::uint64_t integerPart = absValue / 100000000ull;
    const std::uint64_t fractionPart = absValue % 100000000ull;
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString{})
        .arg(integerPart)
        .arg(fractionPart, 8, 10, QLatin1Char('0'));
}

QString formatTrimmedE8(std::int64_t value) {
    QString text = formatScaledE8(value);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return text;
}

std::int64_t multiplyScaledE8(std::int64_t lhsE8, std::int64_t rhsE8) {
    constexpr std::int64_t kScale = 100000000ll;
    const bool negative = (lhsE8 < 0) != (rhsE8 < 0);

    std::uint64_t lhsAbs = lhsE8 < 0
        ? static_cast<std::uint64_t>(-(lhsE8 + 1)) + 1u
        : static_cast<std::uint64_t>(lhsE8);
    std::uint64_t rhsAbs = rhsE8 < 0
        ? static_cast<std::uint64_t>(-(rhsE8 + 1)) + 1u
        : static_cast<std::uint64_t>(rhsE8);

    const std::uint64_t lhsInt = lhsAbs / static_cast<std::uint64_t>(kScale);
    const std::uint64_t lhsFrac = lhsAbs % static_cast<std::uint64_t>(kScale);
    const std::uint64_t rhsInt = rhsAbs / static_cast<std::uint64_t>(kScale);
    const std::uint64_t rhsFrac = rhsAbs % static_cast<std::uint64_t>(kScale);

    const std::uint64_t resultAbs =
        lhsInt * rhsInt * static_cast<std::uint64_t>(kScale)
        + lhsInt * rhsFrac
        + rhsInt * lhsFrac
        + (lhsFrac * rhsFrac) / static_cast<std::uint64_t>(kScale);

    if (!negative) return static_cast<std::int64_t>(resultAbs);
    return -static_cast<std::int64_t>(resultAbs);
}

QString formatTimeNs(std::int64_t tsNs) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
}

}  // namespace

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAntialiasing(true);
}

ChartItem::~ChartItem() = default;

void ChartItem::setController(ChartController* c) {
    if (controller_ == c) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged,
                this, &ChartItem::requestRepaint);
        connect(controller_, &ChartController::sessionChanged,
                this, &ChartItem::requestRepaint);
    }
    emit controllerChanged();
    update();
}

void ChartItem::setTradesVisible(bool value) {
    if (tradesVisible_ == value) return;
    tradesVisible_ = value;
    if (!tradesVisible_) clearHover();
    emit tradesVisibleChanged();
    update();
}

void ChartItem::setHoverPoint(qreal x, qreal y) {
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    updateHover_();
    update();
}

void ChartItem::clearHover() {
    hoverActive_ = false;
    hoveredTradeIndex_ = -1;
    update();
}

void ChartItem::requestRepaint() {
    updateHover_();
    update();
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    if (!tradesVisible_ || !hoverActive_ || !controller_ || !controller_->loaded() || width() <= 0 || height() <= 0) return;

    ViewportMap vp{
        controller_->tsMin(),
        controller_->tsMax(),
        controller_->priceMinE8(),
        controller_->priceMaxE8(),
        width(),
        height(),
    };
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin) return;

    constexpr double hitRadiusPx = 9.0;
    const double hitRadiusSq = hitRadiusPx * hitRadiusPx;
    double bestDistanceSq = hitRadiusSq;
    const auto& trades = controller_->replay().trades();
    for (int i = 0; i < static_cast<int>(trades.size()); ++i) {
        const auto& trade = trades[static_cast<std::size_t>(i)];
        if (trade.tsNs < vp.tMin || trade.tsNs > vp.tMax) continue;
        if (trade.priceE8 < vp.pMin || trade.priceE8 > vp.pMax) continue;

        const double dx = vp.toX(trade.tsNs) - hoverPoint_.x();
        const double dy = vp.toY(trade.priceE8) - hoverPoint_.y();
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            hoveredTradeIndex_ = i;
        }
    }
}

void ChartItem::paint(QPainter* painter) {
    const QRectF rect = boundingRect();
    painter->fillRect(rect, bgColor());

    if (!controller_ || width() <= 0 || height() <= 0) return;

    ViewportMap vp{
        controller_->tsMin(),
        controller_->tsMax(),
        controller_->priceMinE8(),
        controller_->priceMaxE8(),
        width(),
        height(),
    };
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin) return;

    if (!controller_->loaded()) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, vp.w - 16, 24},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Pick a session, then load Trades."));
        return;
    }

    const auto& replay = controller_->replay();
    constexpr double radius = 0.8;
    if (tradesVisible_) {
        QPen linePen(tradeOutlineColor());
        linePen.setWidthF(0.45);
        linePen.setColor(QColor(150, 150, 155, 120));
        painter->setPen(linePen);

        bool havePreviousPoint = false;
        QPointF previousPoint;
        for (const auto& trade : replay.trades()) {
            if (trade.tsNs < vp.tMin || trade.tsNs > vp.tMax) continue;
            if (trade.priceE8 < vp.pMin || trade.priceE8 > vp.pMax) continue;

            const double x = vp.toX(trade.tsNs);
            const double y = vp.toY(trade.priceE8);
            const QPointF point{x, y};
            if (havePreviousPoint) painter->drawLine(previousPoint, point);
            previousPoint = point;
            havePreviousPoint = true;

            QColor fill = trade.sideBuy ? tradeBuyColor() : tradeSellColor();
            fill.setAlpha(235);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawEllipse(point, radius, radius);
            painter->setPen(linePen);
        }
    }

    if (tradesVisible_ && hoveredTradeIndex_ >= 0 && hoveredTradeIndex_ < static_cast<int>(replay.trades().size())) {
        const auto& trade = replay.trades()[static_cast<std::size_t>(hoveredTradeIndex_)];
        if (trade.tsNs >= vp.tMin && trade.tsNs <= vp.tMax && trade.priceE8 >= vp.pMin && trade.priceE8 <= vp.pMax) {
            const QPointF center{vp.toX(trade.tsNs), vp.toY(trade.priceE8)};
            const QColor accent = trade.sideBuy ? tradeBuyColor() : tradeSellColor();
            const auto amountE8 = multiplyScaledE8(trade.qtyE8, trade.priceE8);

            QPen haloPen(trade.sideBuy ? haloBuyColor() : haloSellColor());
            haloPen.setWidthF(3.0);
            painter->setPen(haloPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, radius + 3.5, radius + 3.5);

            QPen focusPen(accent);
            focusPen.setWidthF(1.3);
            painter->setPen(focusPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, radius + 1.7, radius + 1.7);

            QStringList lines;
            lines << QStringLiteral("%1 trade").arg(trade.sideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"));
            lines << QStringLiteral("Price  %1").arg(formatScaledE8(trade.priceE8));
            lines << QStringLiteral("Qty    %1").arg(formatTrimmedE8(trade.qtyE8));
            lines << QStringLiteral("Amount %1").arg(formatScaledE8(amountE8));
            lines << QStringLiteral("Time   %1").arg(formatTimeNs(trade.tsNs));

            QFont tooltipFont = painter->font();
            tooltipFont.setPixelSize(12);
            painter->setFont(tooltipFont);
            const QFontMetrics metrics(tooltipFont);
            int textWidth = 0;
            for (const auto& line : lines) {
                textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
            }

            const int lineHeight = metrics.height();
            const qreal paddingX = 12.0;
            const qreal paddingY = 10.0;
            const qreal cardWidth = static_cast<qreal>(textWidth) + paddingX * 2.0;
            const qreal cardHeight = static_cast<qreal>(lineHeight * lines.size()) + paddingY * 2.0;

            qreal cardX = center.x() + 14.0;
            qreal cardY = center.y() - cardHeight - 14.0;
            if (cardX + cardWidth > vp.w - 8.0) cardX = center.x() - cardWidth - 14.0;
            if (cardX < 8.0) cardX = 8.0;
            if (cardY < 8.0) cardY = center.y() + 14.0;
            if (cardY + cardHeight > vp.h - 8.0) cardY = vp.h - cardHeight - 8.0;

            QRectF cardRect{cardX, cardY, cardWidth, cardHeight};
            painter->setPen(QPen(tooltipBorderColor(), 1.0));
            painter->setBrush(tooltipBackColor());
            painter->drawRoundedRect(cardRect, 8.0, 8.0);

            QRectF accentRect{cardRect.left(), cardRect.top(), 4.0, cardRect.height()};
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent);
            painter->drawRoundedRect(accentRect, 8.0, 8.0);

            painter->setPen(axisTextColor());
            qreal textY = cardRect.top() + paddingY + metrics.ascent();
            for (int i = 0; i < lines.size(); ++i) {
                painter->drawText(QPointF{cardRect.left() + paddingX, textY}, lines[i]);
                textY += lineHeight;
            }
        }
    }
}

}  // namespace hftrec::gui::viewer
