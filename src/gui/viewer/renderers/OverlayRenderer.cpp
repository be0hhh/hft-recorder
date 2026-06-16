#include "gui/viewer/renderers/OverlayRenderer.hpp"

#include <algorithm>
#include <cstdint>

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QLatin1Char>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QStringList>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/BookMath.hpp"
#include "gui/viewer/detail/TradeGrouping.hpp"

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

qreal fundingStripY(const RenderSnapshot& snap) noexcept {
    if (snap.vp.h <= 36.0) return std::max<qreal>(8.0, snap.vp.h * 0.5);
    return std::clamp<qreal>(snap.vp.h - 18.0, 18.0, snap.vp.h - 8.0);
}

QString formatCompactTimeNs(std::int64_t tsNs) {
    if (tsNs <= 0) return QStringLiteral("-");
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(tsNs / 1000000ll), Qt::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

QString formatFundingRatePercent(std::int64_t rateE8) {
    const bool negative = rateE8 < 0;
    const std::uint64_t magnitude = negative
        ? static_cast<std::uint64_t>(-(rateE8 + 1)) + 1u
        : static_cast<std::uint64_t>(rateE8);
    constexpr std::uint64_t kScale = 1000000u;
    const std::uint64_t whole = magnitude / kScale;
    const std::uint64_t frac = magnitude % kScale;
    return QStringLiteral("%1%2.%3%")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(static_cast<qulonglong>(whole))
        .arg(static_cast<qulonglong>(frac), 6, 10, QLatin1Char('0'));
}

QString formatCadence(std::int64_t cadenceNs) {
    if (cadenceNs <= 0) return {};
    constexpr std::int64_t kNsPerMinute = 60ll * 1000000000ll;
    const std::int64_t totalMinutes = (cadenceNs + kNsPerMinute / 2) / kNsPerMinute;
    if (totalMinutes <= 0) return {};
    const std::int64_t hours = totalMinutes / 60;
    const std::int64_t minutes = totalMinutes % 60;
    if (hours > 0 && minutes > 0) return QStringLiteral("~%1h %2m").arg(hours).arg(minutes);
    if (hours > 0) return QStringLiteral("~%1h").arg(hours);
    return QStringLiteral("~%1m").arg(minutes);
}

QString sourceLabel(const RenderSnapshot& snap) {
    QStringList parts;
    if (!snap.sourceExchange.isEmpty()) parts << snap.sourceExchange;
    if (!snap.sourceMarket.isEmpty()) parts << snap.sourceMarket;
    if (!snap.sourceSymbol.isEmpty()) parts << snap.sourceSymbol;
    return parts.isEmpty() ? QStringLiteral("unknown") : parts.join(QLatin1Char(' '));
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

void renderFundingOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov = ctx.hov;
    if (!hov.contextActive || !snap.fundingVisible || !hov.fundingHit) return;
    if (hov.tradeHit || hov.liquidationHit || hov.strategyFillHit || hov.bookKind != 0) return;
    if (hov.fundingEventTsNs < snap.vp.tMin || hov.fundingEventTsNs > snap.vp.tMax) return;

    const QColor accent{255, 214, 51};
    const qreal x = std::clamp<qreal>(snap.vp.toX(hov.fundingEventTsNs), 0.0, snap.vp.w);
    const qreal y = fundingStripY(snap);

    QPen focusPen(accent);
    focusPen.setWidthF(1.5);
    focusPen.setCosmetic(true);
    ctx.p->setPen(focusPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawLine(QPointF{x, std::max<qreal>(0.0, y - 12.0)}, QPointF{x, std::min<qreal>(snap.vp.h, y + 12.0)});
    ctx.p->drawEllipse(QPointF{x, y}, 5.0, 5.0);

    QStringList lines;
    lines << QStringLiteral("Funding");
    lines << QStringLiteral("Venue  %1").arg(sourceLabel(snap));
    lines << QStringLiteral("Rate   %1").arg(formatFundingRatePercent(hov.fundingRateE8));
    lines << QStringLiteral("Event  %1").arg(formatCompactTimeNs(hov.fundingEventTsNs));
    lines << QStringLiteral("Time   %1").arg(formatCompactTimeNs(hov.fundingTsNs));
    lines << QStringLiteral("Next   %1").arg(formatCompactTimeNs(hov.nextFundingTsNs));
    const QString cadence = formatCadence(hov.fundingCadenceNs);
    if (!cadence.isEmpty()) lines << QStringLiteral("Every  %1").arg(cadence);

    renderObjectCard(ctx.p, lines, accent, x, y, snap.vp.w, snap.vp.h);
}

void renderBookOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov  = ctx.hov;
    if (!hov.contextActive) return;
    if (!(snap.bookTickerVisible || snap.orderbookVisible)) return;
    if (hov.tradeHit || hov.liquidationHit) return;
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

void renderLiquidationOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov = ctx.hov;
    if (!hov.contextActive || !snap.liquidationsVisible || !hov.liquidationHit) return;
    if (hov.liquidationTsNs < snap.vp.tMin || hov.liquidationTsNs > snap.vp.tMax) return;
    if (hov.liquidationPriceE8 < snap.vp.pMin || hov.liquidationPriceE8 > snap.vp.pMax) return;

    const QPointF center{snap.vp.toX(hov.liquidationTsNs), snap.vp.toY(hov.liquidationPriceE8)};
    const QColor accent = hov.liquidationSideBuy ? QColor(255, 221, 0) : QColor(255, 255, 255);
    const auto amountE8 = detail::multiplyScaledE8(hov.liquidationQtyE8, hov.liquidationPriceE8);
    const qreal radius = detail::amountRadiusScale(amountE8, snap.tradeAmountScale, false);

    QPen haloPen(accent);
    haloPen.setWidthF(3.0);
    ctx.p->setPen(haloPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawEllipse(center, radius + 3.5, radius + 3.5);

    QStringList lines;
    lines << QStringLiteral("%1 liquidation")
                 .arg(hov.liquidationSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"));
    lines << QStringLiteral("Price     %1").arg(detail::formatTrimmedE8(hov.liquidationPriceE8));
    lines << QStringLiteral("Qty       %1").arg(detail::formatTrimmedE8(hov.liquidationQtyE8));
    lines << QStringLiteral("AvgPrice  %1").arg(detail::formatTrimmedE8(hov.liquidationAvgPriceE8));
    lines << QStringLiteral("FilledQty %1").arg(detail::formatTrimmedE8(hov.liquidationFilledQtyE8));
    lines << QStringLiteral("Amount    %1").arg(detail::formatTrimmedE8(amountE8));
    lines << QStringLiteral("Time      %1").arg(detail::formatTimeNs(hov.liquidationTsNs));

    renderObjectCard(ctx.p, lines, accent, center.x(), center.y(), snap.vp.w, snap.vp.h);
}
void renderStrategyFillOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov = ctx.hov;
    if (!hov.contextActive || !hov.strategyFillHit) return;
    if (hov.strategyFillTsNs < snap.vp.tMin || hov.strategyFillTsNs > snap.vp.tMax) return;
    if (hov.strategyFillPriceE8 < snap.vp.pMin || hov.strategyFillPriceE8 > snap.vp.pMax) return;

    const QPointF center{snap.vp.toX(hov.strategyFillTsNs), snap.vp.toY(hov.strategyFillPriceE8)};
    const QColor accent = hov.strategyFillSideBuy ? QColor(0xFF, 0xD8, 0x4D) : QColor(0xFF, 0x1A, 0xC8);

    QPen focusPen(accent);
    focusPen.setWidthF(2.0);
    focusPen.setCosmetic(true);
    ctx.p->setPen(focusPen);
    ctx.p->setBrush(Qt::NoBrush);
    ctx.p->drawEllipse(center, 8.0, 8.0);

    QStringList lines;
    lines << QStringLiteral("My %1 %2")
                 .arg(hov.strategyFillSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"))
                 .arg(hov.strategyFillReduceOnly ? QStringLiteral("close") : QStringLiteral("fill"));
    lines << QStringLiteral("Price  %1").arg(detail::formatTrimmedE8(hov.strategyFillPriceE8));
    lines << QStringLiteral("Qty    %1").arg(detail::formatTrimmedE8(hov.strategyFillQtyE8));
    lines << QStringLiteral("Amount %1").arg(detail::formatTrimmedE8(hov.strategyFillAmountE8));
    lines << QStringLiteral("Time   %1").arg(detail::formatTimeNs(hov.strategyFillTsNs));

    renderObjectCard(ctx.p, lines, accent, center.x(), center.y(), snap.vp.w, snap.vp.h);
}

void renderTradeOverlay(const RenderContext& ctx) {
    const auto& snap = ctx.s;
    const auto& hov  = ctx.hov;
    if (!hov.contextActive || !snap.tradesVisible || !hov.tradeHit) return;
    if (hov.tradeTsNs < snap.vp.tMin || hov.tradeTsNs > snap.vp.tMax) return;
    if (hov.tradePriceE8 < snap.vp.pMin || hov.tradePriceE8 > snap.vp.pMax) return;

    const QPointF center{snap.vp.toX(hov.tradeTsNs), snap.vp.toY(hov.tradePriceE8)};
    const QColor accent = hov.tradeAggregated ? tradeAggregateColor() : (hov.tradeSideBuy ? tradeBuyColor() : tradeSellColor());
    const auto amountE8 = hov.tradeTotalAmountE8 != 0
        ? hov.tradeTotalAmountE8
        : detail::multiplyScaledE8(hov.tradeQtyE8, hov.tradePriceE8);
    const qreal tradeRadius = detail::amountRadiusScale(amountE8, snap.tradeAmountScale, false);

    QPen haloPen(hov.tradeAggregated ? tradeAggregateColor() : (hov.tradeSideBuy ? haloBuyColor() : haloSellColor()));
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
    if (hov.tradeAggregated) {
        lines << QStringLiteral("Trades %1 aggregated").arg(hov.tradeCount);
        lines << QStringLiteral("VWAP   %1").arg(detail::formatTrimmedE8(hov.tradePriceE8));
        lines << QStringLiteral("Total Qty    %1").arg(detail::formatTrimmedE8(hov.tradeTotalQtyE8));
        lines << QStringLiteral("Total Amount %1").arg(detail::formatTrimmedE8(amountE8));
        lines << QStringLiteral("Buy Qty      %1").arg(detail::formatTrimmedE8(hov.tradeBuyQtyE8));
        lines << QStringLiteral("Sell Qty     %1").arg(detail::formatTrimmedE8(hov.tradeSellQtyE8));
        lines << QStringLiteral("Time   %1 -> %2")
                     .arg(detail::formatTimeNs(hov.tradeTsStartNs))
                     .arg(detail::formatTimeNs(hov.tradeTsEndNs));
        lines << QStringLiteral("Largest %1 %2 x %3")
                     .arg(hov.tradeSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"))
                     .arg(detail::formatTrimmedE8(hov.tradeQtyE8))
                     .arg(detail::formatTrimmedE8(hov.tradeRepresentativePriceE8));
    } else if (hov.tradeGroupEntries.size() <= 1u) {
        lines << QStringLiteral("%1 trade")
                     .arg(hov.tradeSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"));
        lines << QStringLiteral("Price  %1").arg(detail::formatTrimmedE8(hov.tradePriceE8));
        lines << QStringLiteral("Qty    %1").arg(detail::formatTrimmedE8(hov.tradeQtyE8));
        lines << QStringLiteral("Amount %1").arg(detail::formatTrimmedE8(amountE8));
        lines << QStringLiteral("Time   %1").arg(detail::formatTimeNs(hov.tradeTsNs));
    } else {
        constexpr int kMaxTooltipTrades = 12;
        lines << QStringLiteral("Trades %1 @ same timestamp").arg(hov.tradeGroupEntries.size());
        lines << QStringLiteral("Total Qty    %1").arg(detail::formatTrimmedE8(hov.tradeTotalQtyE8));
        lines << QStringLiteral("Total Amount %1").arg(detail::formatTrimmedE8(amountE8));
        lines << QStringLiteral("Time         %1").arg(detail::formatTimeNs(hov.tradeTsNs));
        lines << QStringLiteral("Largest      %1 %2 x %3")
                     .arg(hov.tradeSideBuy ? QStringLiteral("BUY") : QStringLiteral("SELL"))
                     .arg(detail::formatTrimmedE8(hov.tradeQtyE8))
                     .arg(detail::formatTrimmedE8(hov.tradePriceE8));
        lines << QString{};
        const int shown = std::min<int>(static_cast<int>(hov.tradeGroupEntries.size()), kMaxTooltipTrades);
        for (int i = 0; i < shown; ++i) {
            const auto& entry = hov.tradeGroupEntries[static_cast<std::size_t>(i)];
            lines << QStringLiteral("%1. %2 %3 x %4 = %5")
                         .arg(i + 1)
                         .arg(entry.sideBuy ? QStringLiteral("BUY ") : QStringLiteral("SELL"))
                         .arg(detail::formatTrimmedE8(entry.qtyE8))
                         .arg(detail::formatTrimmedE8(entry.priceE8))
                         .arg(detail::formatTrimmedE8(entry.amountE8));
        }
        if (shown < static_cast<int>(hov.tradeGroupEntries.size())) {
            lines << QStringLiteral("... %1 more").arg(static_cast<int>(hov.tradeGroupEntries.size()) - shown);
        }
    }

    renderObjectCard(ctx.p, lines, accent, center.x(), center.y(), snap.vp.w, snap.vp.h);
}

}  // namespace

void renderOverlay(const RenderContext& ctx) {
    renderVerticalMarkers(ctx);
    renderFundingOverlay(ctx);
    renderBookOverlay(ctx);
    renderLiquidationOverlay(ctx);
    renderStrategyFillOverlay(ctx);
    renderTradeOverlay(ctx);
}

}  // namespace hftrec::gui::viewer::renderers
