#include "gui/viewer/renderers/OverlayRenderer.hpp"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QStringList>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::renderers {

namespace {

void drawTextCard(QPainter* painter, const QRectF& card, const QColor& accent,
                  const QStringList& lines) {
    painter->setPen(QPen(tooltipBorderColor(), 1.0));
    painter->setBrush(tooltipBackColor());
    painter->drawRoundedRect(card, 8.0, 8.0);

    QRectF accentRect{card.left(), card.top(), 4.0, card.height()};
    painter->setPen(Qt::NoPen);
    painter->setBrush(accent);
    painter->drawRoundedRect(accentRect, 8.0, 8.0);

    QFont f = painter->font();
    f.setPixelSize(12);
    painter->setFont(f);
    const QFontMetrics metrics(f);

    painter->setPen(axisTextColor());
    qreal textY = card.top() + 10.0 + metrics.ascent();
    for (const auto& line : lines) {
        painter->drawText(QPointF{card.left() + 12.0, textY}, line);
        textY += metrics.height();
    }
}

QRectF layoutCard(const QStringList& lines, const QFontMetrics& metrics,
                  qreal anchorX, qreal anchorY, qreal vpW, qreal vpH) {
    int textWidth = 0;
    for (const auto& line : lines) {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
    }
    const qreal paddingX = 12.0;
    const qreal paddingY = 10.0;
    const qreal w = static_cast<qreal>(textWidth) + paddingX * 2.0;
    const qreal h = static_cast<qreal>(metrics.height() * lines.size()) + paddingY * 2.0;
    qreal x = anchorX + 14.0;
    qreal y = anchorY - h - 14.0;
    if (x + w > vpW - 8.0) x = anchorX - w - 14.0;
    if (x < 8.0) x = 8.0;
    if (y < 8.0) y = anchorY + 14.0;
    if (y + h > vpH - 8.0) y = vpH - h - 8.0;
    return QRectF{x, y, w, h};
}

void renderObjectCard(QPainter* painter,
                      const QStringList& lines,
                      const QColor& accent,
                      qreal anchorX,
                      qreal anchorY,
                      qreal vpW,
                      qreal vpH) {
    QFont f = painter->font();
    f.setPixelSize(12);
    painter->setFont(f);
    const QFontMetrics metrics(f);
    const QRectF card = layoutCard(lines, metrics, anchorX, anchorY, vpW, vpH);
    drawTextCard(painter, card, accent, lines);
}


void renderVerticalMarkers(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    if (snap.verticalMarkers.empty()) return;

    QPen markerPen(QColor(0xFF, 0xD8, 0x4D, 0xE8));
    markerPen.setWidthF(2.0);
    markerPen.setCosmetic(true);
    ctx.p->setPen(markerPen);
    ctx.p->setBrush(Qt::NoBrush);

    QFont labelFont = ctx.p->font();
    labelFont.setPixelSize(11);
    ctx.p->setFont(labelFont);
    const QFontMetrics metrics(labelFont);

    for (const auto& marker : snap.verticalMarkers) {
        if (marker.tsNs < snap.vp.tMin || marker.tsNs > snap.vp.tMax) continue;
        const qreal x = snap.vp.toX(marker.tsNs);
        if (x < 0.0 || x > snap.vp.w) continue;
        ctx.p->drawLine(QPointF{x, 0.0}, QPointF{x, snap.vp.h});
        if (!marker.label.isEmpty()) {
            const QString label = marker.label.left(64);
            const qreal textW = static_cast<qreal>(metrics.horizontalAdvance(label));
            qreal tx = x + 6.0;
            if (tx + textW > snap.vp.w - 6.0) tx = x - textW - 6.0;
            tx = std::clamp<qreal>(tx, 6.0, std::max<qreal>(6.0, snap.vp.w - textW - 6.0));
            ctx.p->setPen(QColor(0xFF, 0xD8, 0x4D, 0xF5));
            ctx.p->drawText(QPointF{tx, 16.0}, label);
            ctx.p->setPen(markerPen);
        }
    }
}
void renderBookOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov  = ctx.hov;
    if (!hov.contextActive) return;
    if (!(snap.bookTickerVisible || snap.orderbookVisible)) return;
    if (hov.tradeHit) return;
    if (hov.bookKind == 0) return;

    const bool isBid = hov.bookKind == 1 || hov.bookKind == 3;
    const QPointF center{
        std::clamp<qreal>(hov.point.x(), 0.0, snap.vp.w),
        std::clamp<qreal>(snap.vp.toY(hov.bookPriceE8), 0.0, snap.vp.h)
    };
    const QColor accent = isBid ? bidColor() : askColor();

    if (hov.bookKind >= 3 && hov.bookTsEndNs > hov.bookTsStartNs) {
        QPen spanPen(accent);
        spanPen.setWidthF(1.6);
        ctx.p->setPen(spanPen);
        ctx.p->setBrush(Qt::NoBrush);
        ctx.p->drawLine(
            QPointF{snap.vp.toX(hov.bookTsStartNs), center.y()},
            QPointF{snap.vp.toX(hov.bookTsEndNs), center.y()});
    }

    QPen focusPen(accent);
    focusPen.setWidthF(1.4);
    ctx.p->setPen(focusPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawEllipse(center, 7.0, 7.0);

    QPen innerPen(accent);
    innerPen.setWidthF(1.0);
    ctx.p->setPen(innerPen);
    ctx.p->setBrush(accent);
    ctx.p->drawEllipse(center, 2.2, 2.2);

    QStringList lines;
    lines << QStringLiteral("%1 %2")
                 .arg(isBid ? QStringLiteral("BID") : QStringLiteral("ASK"))
                 .arg(hov.bookKind <= 2 ? QStringLiteral("ticker") : QStringLiteral("book"));
    lines << QStringLiteral("Price  %1").arg(detail::formatTrimmedE8(hov.bookPriceE8));
    lines << QStringLiteral("Qty    %1").arg(detail::formatTrimmedE8(hov.bookQtyE8));
    lines << QStringLiteral("Amount %1")
                 .arg(detail::formatTrimmedE8(detail::multiplyScaledE8(hov.bookQtyE8, hov.bookPriceE8)));
    if (hov.bookKind >= 3 && hov.bookTsEndNs > hov.bookTsStartNs) {
        lines << QStringLiteral("Time   %1 -> %2")
                     .arg(detail::formatTimeNs(hov.bookTsStartNs))
                     .arg(detail::formatTimeNs(hov.bookTsEndNs));
    } else {
        lines << QStringLiteral("Time   %1").arg(detail::formatTimeNs(hov.bookTsNs));
    }

    renderObjectCard(ctx.p, lines, accent, center.x(), center.y(), snap.vp.w, snap.vp.h);
}

void renderTradeOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov  = ctx.hov;
    if (!hov.contextActive || !snap.tradesVisible || !hov.tradeHit) return;
    if (hov.tradeTsNs < snap.vp.tMin || hov.tradeTsNs > snap.vp.tMax) return;
    if (hov.tradePriceE8 < snap.vp.pMin || hov.tradePriceE8 > snap.vp.pMax) return;

    const QPointF center{snap.vp.toX(hov.tradeTsNs), snap.vp.toY(hov.tradePriceE8)};
    const QColor accent = hov.tradeSideBuy ? tradeBuyColor() : tradeSellColor();
    const auto amountE8 = detail::multiplyScaledE8(hov.tradeQtyE8, hov.tradePriceE8);
    const qreal tradeRadius = detail::amountRadiusScale(amountE8, snap.tradeAmountScale, false);

    QPen haloPen(hov.tradeSideBuy ? haloBuyColor() : haloSellColor());
    haloPen.setWidthF(3.0);
    ctx.p->setPen(haloPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawEllipse(center, tradeRadius + 3.5, tradeRadius + 3.5);

    QPen focusPen(accent);
    focusPen.setWidthF(1.3);
    ctx.p->setPen(focusPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawEllipse(center, tradeRadius + 1.7, tradeRadius + 1.7);

    QStringList lines;
    lines << QStringLiteral("%1 trade")
                 .arg(hov.tradeSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"));
    lines << QStringLiteral("Price  %1").arg(detail::formatTrimmedE8(hov.tradePriceE8));
    lines << QStringLiteral("Qty    %1").arg(detail::formatTrimmedE8(hov.tradeQtyE8));
    lines << QStringLiteral("Amount %1").arg(detail::formatTrimmedE8(amountE8));
    lines << QStringLiteral("Time   %1").arg(detail::formatTimeNs(hov.tradeTsNs));

    renderObjectCard(ctx.p, lines, accent, center.x(), center.y(), snap.vp.w, snap.vp.h);
}

}  // namespace

void renderOverlay(const RenderContext& ctx) {
    renderVerticalMarkers(ctx);
    renderBookOverlay(ctx);
    renderTradeOverlay(ctx);
}

}  // namespace hftrec::gui::viewer::renderers
