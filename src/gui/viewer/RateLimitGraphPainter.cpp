#include "gui/viewer/RateLimitGraphPainter.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QString>

#include <algorithm>
#include <vector>

#include "gui/viewer/RateLimitUsage.hpp"

namespace hftrec::gui::viewer {
namespace {

constexpr std::int64_t kPct100E4 = 1000000ll;

qreal mapX(std::int64_t tsNs, std::int64_t tsMin, std::int64_t tsMax, qreal left, qreal width) noexcept {
    if (tsMax <= tsMin) return left;
    const qreal ratio = static_cast<qreal>(tsNs - tsMin) / static_cast<qreal>(tsMax - tsMin);
    return left + ratio * width;
}

std::int64_t clampPctE4(std::int64_t pctE4) noexcept {
    return std::clamp<std::int64_t>(pctE4, 0, kPct100E4);
}

qreal mapPctY(std::int64_t pctE4, const QRectF& rect) noexcept {
    const qreal ratio = static_cast<qreal>(clampPctE4(pctE4)) / static_cast<qreal>(kPct100E4);
    return rect.bottom() - ratio * rect.height();
}

QString pctText(std::int64_t pctE4) {
    const std::int64_t value = clampPctE4(pctE4);
    const std::int64_t whole = value / 10000ll;
    const std::int64_t frac = (value % 10000ll) / 100ll;
    if (frac == 0) return QStringLiteral("%1%").arg(whole);
    return QStringLiteral("%1.%2%").arg(whole).arg(frac, 2, 10, QLatin1Char('0'));
}

void drawLaneGrid(QPainter& painter, const QRectF& laneRect, const QColor& gridColor) {
    QPen gridPen{gridColor};
    gridPen.setStyle(Qt::DashLine);
    painter.setPen(gridPen);
    painter.drawLine(QPointF{laneRect.left(), laneRect.top()}, QPointF{laneRect.right(), laneRect.top()});
    painter.drawLine(QPointF{laneRect.left(), laneRect.center().y()}, QPointF{laneRect.right(), laneRect.center().y()});
    painter.drawLine(QPointF{laneRect.left(), laneRect.bottom()}, QPointF{laneRect.right(), laneRect.bottom()});
}

}  // namespace

void drawRateLimitGraph(QPainter& painter,
                        const QRectF& bounds,
                        const RateLimitUsageData& usage,
                        std::int64_t tsMin,
                        std::int64_t tsMax,
                        const RateLimitGraphOptions& options) {
    if (bounds.width() <= 1.0 || bounds.height() <= 1.0) return;
    if (options.fillBackground) painter.fillRect(bounds, QColor{"#111318"});
    if (usage.points.empty() || tsMax <= tsMin) return;

    const std::uint32_t lanes = std::max<std::uint32_t>(1u, usage.legCount);
    const qreal leftLabel = options.showLaneLabels ? 40.0 : 4.0;
    const qreal rightLabel = options.showLastValues ? 48.0 : 4.0;
    const qreal plotLeft = bounds.left() + leftLabel;
    const qreal plotRight = bounds.right() - rightLabel;
    const qreal plotWidth = std::max<qreal>(1.0, plotRight - plotLeft);

    const QColor lineColor{"#8ff5b6"};
    const QColor fillColor{80, 220, 132, 38};
    const QColor gridColor{"#2b3038"};
    const QColor mutedColor{"#818995"};

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(QFont{painter.font().family(), 9});

    const QRectF rowRect{bounds.left(), bounds.top(), bounds.width(), bounds.height()};
    painter.fillRect(rowRect, QColor{"#151922"});

    const QRectF plotRect{plotLeft, rowRect.top() + 5.0, plotWidth, std::max<qreal>(4.0, rowRect.height() - 10.0)};
    drawLaneGrid(painter, plotRect, gridColor);

    if (options.showLaneLabels) {
        painter.setPen(mutedColor);
        painter.drawText(QRectF{bounds.left() + 4.0, rowRect.top(), leftLabel - 8.0, rowRect.height()},
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("min"));
    }

    std::vector<std::int64_t> current(lanes, kPct100E4);
    for (const RateLimitUsagePoint& point : usage.points) {
        if (point.tsNs >= tsMin) break;
        if (point.legIndex < lanes) current[point.legIndex] = clampPctE4(point.minRemainingPctE4);
    }

    auto currentPct = *std::min_element(current.begin(), current.end());
    std::int64_t lastTs = tsMin;
    QPolygonF line;
    QPainterPath area;
    const qreal startY = mapPctY(currentPct, plotRect);
    line << QPointF{plotLeft, startY};
    area.moveTo(plotLeft, plotRect.bottom());
    area.lineTo(plotLeft, startY);

    for (std::size_t i = 0; i < usage.points.size();) {
        const std::int64_t ts = usage.points[i].tsNs;
        if (ts < tsMin) {
            ++i;
            continue;
        }
        if (ts > tsMax) break;

        const qreal x = mapX(ts, tsMin, tsMax, plotLeft, plotWidth);
        const qreal y0 = mapPctY(currentPct, plotRect);
        const qreal lastX = mapX(lastTs, tsMin, tsMax, plotLeft, plotWidth);
        if (x > lastX) {
            line << QPointF{x, y0};
            area.lineTo(x, y0);
        }

        while (i < usage.points.size() && usage.points[i].tsNs == ts) {
            const RateLimitUsagePoint& point = usage.points[i];
            if (point.legIndex < lanes) current[point.legIndex] = clampPctE4(point.minRemainingPctE4);
            ++i;
        }
        currentPct = *std::min_element(current.begin(), current.end());
        const qreal y1 = mapPctY(currentPct, plotRect);
        line << QPointF{x, y1};
        area.lineTo(x, y1);
        lastTs = ts;
    }

    const qreal endY = mapPctY(currentPct, plotRect);
    line << QPointF{plotRight, endY};
    area.lineTo(plotRight, endY);
    area.lineTo(plotRight, plotRect.bottom());
    area.closeSubpath();

    painter.fillPath(area, QBrush{fillColor});
    QPen linePen{lineColor};
    linePen.setWidth(2);
    linePen.setCapStyle(Qt::SquareCap);
    linePen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(linePen);
    painter.drawPolyline(line);

    if (options.showLastValues) {
        painter.setPen(lineColor);
        painter.drawText(QRectF{plotRight + 5.0, rowRect.top(), rightLabel - 8.0, rowRect.height()},
                         Qt::AlignRight | Qt::AlignVCenter,
                         pctText(currentPct));
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor{"#343a44"});
    painter.drawRect(bounds.adjusted(0.0, 0.0, -1.0, -1.0));
}

}  // namespace hftrec::gui::viewer
