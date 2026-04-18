#include "gui/viewer/ChartItem.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <algorithm>
#include <vector>

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

void ChartItem::requestRepaint() {
    update();
}

void ChartItem::paint(QPainter* painter) {
    const QRectF r = boundingRect();

    // Background.
    painter->fillRect(r, bgColor());

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

    // Grid.
    QPen gridPen(gridColor());
    gridPen.setWidthF(1.0);
    painter->setPen(gridPen);
    constexpr int verticals   = 10;
    constexpr int horizontals = 8;
    for (int i = 0; i <= verticals; ++i) {
        const double x = (vp.w * i) / verticals;
        painter->drawLine(QPointF{x, 0.0}, QPointF{x, vp.h});
    }
    for (int i = 0; i <= horizontals; ++i) {
        const double y = (vp.h * i) / horizontals;
        painter->drawLine(QPointF{0.0, y}, QPointF{vp.w, y});
    }

    if (!controller_->loaded()) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, vp.w - 16, 24}, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("No session loaded. Pick a session and press Open, "
                                         "or load individual JSONL files."));
        return;
    }

    auto& replay = controller_->replay();
    replay.seek(vp.tMax);

    // Orderbook horizontal lines.
    {
        QPen pen;
        pen.setWidthF(1.0);
        QColor bid = bidColor();
        bid.setAlpha(200);
        QColor ask = askColor();
        ask.setAlpha(200);
        pen.setColor(bid);
        painter->setPen(pen);
        for (const auto& [price, qty] : replay.book().bids()) {
            (void)qty;
            if (price < vp.pMin || price > vp.pMax) continue;
            const double y = vp.toY(price);
            painter->drawLine(QPointF{0.0, y}, QPointF{vp.w, y});
        }
        pen.setColor(ask);
        painter->setPen(pen);
        for (const auto& [price, qty] : replay.book().asks()) {
            (void)qty;
            if (price < vp.pMin || price > vp.pMax) continue;
            const double y = vp.toY(price);
            painter->drawLine(QPointF{0.0, y}, QPointF{vp.w, y});
        }
    }

    // Connector polyline through all visible trades.
    std::vector<QPointF> connectorPts;
    connectorPts.reserve(256);
    for (const auto& t : replay.trades()) {
        if (t.tsNs < vp.tMin || t.tsNs > vp.tMax) continue;
        if (t.priceE8 < vp.pMin || t.priceE8 > vp.pMax) continue;
        connectorPts.emplace_back(vp.toX(t.tsNs), vp.toY(t.priceE8));
    }
    if (connectorPts.size() >= 2) {
        QPen pen(tradeConnectorColor());
        pen.setWidthF(1.2);
        painter->setPen(pen);
        painter->drawPolyline(connectorPts.data(), static_cast<int>(connectorPts.size()));
    }

    // Trade arrows — up-triangle for taker-buy, down-triangle for taker-sell.
    {
        constexpr double halfW = 4.0;
        constexpr double len   = 9.0;
        QPen noPen(Qt::NoPen);
        painter->setPen(noPen);
        for (const auto& t : replay.trades()) {
            if (t.tsNs < vp.tMin || t.tsNs > vp.tMax) continue;
            if (t.priceE8 < vp.pMin || t.priceE8 > vp.pMax) continue;
            const double x = vp.toX(t.tsNs);
            const double y = vp.toY(t.priceE8);
            QPolygonF tri;
            if (t.sideBuy) {
                tri << QPointF{x, y - len}
                    << QPointF{x - halfW, y}
                    << QPointF{x + halfW, y};
                painter->setBrush(tradeBuyColor());
            } else {
                tri << QPointF{x, y + len}
                    << QPointF{x - halfW, y}
                    << QPointF{x + halfW, y};
                painter->setBrush(tradeSellColor());
            }
            painter->drawPolygon(tri);
        }
    }
}

}  // namespace hftrec::gui::viewer
