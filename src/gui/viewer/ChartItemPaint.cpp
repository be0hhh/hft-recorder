#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QImage>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QQuickWindow>
#include <QStringList>

#include <core/replay/BookState.hpp>
#include <core/metrics/Metrics.hpp>
#include <gui/viewer/detail/BookMath.hpp>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/ChartItemPaintInternal.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/BookTickerTraceBuild.hpp"
#include "gui/viewer/detail/TradeGrouping.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
#include "gui/viewer/renderers/CandleRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/StrategyOverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"
#include "core/common/Timing.hpp"

namespace hftrec::gui::viewer {

namespace {

void renderReferenceOverlays(QPainter* painter, const RenderSnapshot& snap, const HoverInfo& hover);

void paintSnapshotFrame(QPainter* painter,
                        const RenderSnapshot& snap,
                        const HoverInfo& hover,
                        double dpr,
                        bool drawBackground) {
    const QRectF rect{0.0, 0.0, snap.vp.w, snap.vp.h};
    if (drawBackground && !snap.overlayOnly) painter->fillRect(rect, bgColor());

    if (!snap.loaded) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, snap.vp.w - 16, 24},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Pick a session to load market data."));
        return;
    }
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return;

    RenderContext ctx{painter, snap, hover, dpr};
    renderers::renderBook(ctx);
    renderers::renderCandles(ctx);
    renderReferenceOverlays(painter, snap, hover);
    renderers::renderTrades(ctx);
    renderers::renderStrategyOverlay(ctx);
    renderers::renderOverlay(ctx);
}

void drawStepLine(QPainter* painter,
                  const std::vector<QPointF>& points,
                  const QColor& color) {
    if (points.empty()) return;
    QPen pen(color);
    pen.setWidth(1);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    QPointF prev = points.front();
    for (std::size_t i = 1u; i < points.size(); ++i) {
        const QPointF point = points[i];
        const QPointF corner{point.x(), prev.y()};
        painter->drawLine(prev, corner);
        painter->drawLine(corner, point);
        prev = point;
    }
}

QString formatNsUtcCompact(std::int64_t tsNs) {
    if (tsNs <= 0) return QStringLiteral("-");
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(tsNs / 1000000ll), Qt::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
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

qreal fundingStripY(const RenderSnapshot& snap) noexcept {
    if (snap.vp.h <= 36.0) return std::max<qreal>(8.0, snap.vp.h * 0.5);
    return std::clamp<qreal>(snap.vp.h - 18.0, 18.0, snap.vp.h - 8.0);
}

bool fundingStripContextHit(const RenderSnapshot& snap, const HoverInfo& hover, qreal y) noexcept {
    if (!hover.active || !hover.contextActive) return false;
    constexpr qreal kStripHitPx = 12.0;
    constexpr qreal kAxisReserveRight = 88.0;
    const qreal stripLeft = 8.0;
    const qreal stripRight = std::max<qreal>(stripLeft, snap.vp.w - kAxisReserveRight);
    return hover.point.x() >= stripLeft - kStripHitPx
        && hover.point.x() <= stripRight + kStripHitPx
        && std::abs(hover.point.y() - y) <= kStripHitPx;
}

void renderFundingStrip(QPainter* painter, const RenderSnapshot& snap, const HoverInfo& hover, bool drawStrip) {
    if (!snap.fundingVisible || snap.fundings.empty() || snap.vp.w <= 0.0 || snap.vp.h <= 0.0) return;

    painter->save();
    constexpr qreal kAxisReserveRight = 88.0;
    constexpr qreal kAxisReserveBottom = 28.0;
    const QColor fundingColor{255, 214, 51};
    const qreal stripLeft = 8.0;
    const qreal stripRight = std::max<qreal>(stripLeft, snap.vp.w - kAxisReserveRight);
    const qreal y = fundingStripY(snap);

    if (drawStrip) {
        QPen stripPen(QColor{255, 214, 51, 86});
        stripPen.setWidthF(1.0);
        stripPen.setCosmetic(true);
        painter->setPen(stripPen);
        painter->drawLine(QPointF{stripLeft, y}, QPointF{stripRight, y});

        QPen tickPen(QColor{255, 214, 51, 190});
        tickPen.setWidthF(1.0);
        tickPen.setCosmetic(true);
        painter->setPen(tickPen);
        for (const auto& row : snap.fundings) {
            if (row.tsNs < snap.vp.tMin || row.tsNs > snap.vp.tMax) continue;
            const qreal x = std::round(snap.vp.toX(row.tsNs));
            if (x < stripLeft || x > stripRight) continue;
            const qreal halfHeight = row.fundingRateE8 < 0 ? 3.0 : 5.0;
            painter->drawLine(QPointF{x, y - halfHeight}, QPointF{x, y + halfHeight});
        }
    }

    if (!fundingStripContextHit(snap, hover, y)) {
        painter->restore();
        return;
    }

    QFont font = painter->font();
    font.setPixelSize(11);
    painter->setFont(font);
    const QFontMetrics metrics(font);

    const std::size_t start = snap.fundings.size() > 5u ? snap.fundings.size() - 5u : 0u;
    QStringList lines;
    lines << QStringLiteral("Funding");
    for (std::size_t i = start; i < snap.fundings.size(); ++i) {
        const auto& row = snap.fundings[i];
        lines << QStringLiteral("%1  %2")
                     .arg(formatFundingRatePercent(row.fundingRateE8),
                          formatNsUtcCompact(row.nextFundingTsNs));
    }

    int textWidth = 0;
    for (const auto& line : lines) {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
    }
    const qreal paddingX = 12.0;
    const qreal paddingY = 9.0;
    const qreal cardW = static_cast<qreal>(textWidth) + paddingX * 2.0;
    const qreal cardH = static_cast<qreal>(metrics.height() * lines.size()) + paddingY * 2.0;

    qreal cardX = std::min<qreal>(hover.point.x() + 14.0, snap.vp.w - kAxisReserveRight - cardW);
    if (cardX < 8.0) cardX = 8.0;
    qreal cardY = y - cardH - 14.0;
    if (cardY < 8.0) cardY = y + 14.0;
    if (cardY + cardH > snap.vp.h - kAxisReserveBottom) {
        cardY = std::max<qreal>(8.0, snap.vp.h - kAxisReserveBottom - cardH);
    }

    const QRectF card{cardX, cardY, cardW, cardH};
    painter->setPen(QPen(tooltipBorderColor(), 1.0));
    painter->setBrush(tooltipBackColor());
    painter->drawRoundedRect(card, 6.0, 6.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fundingColor);
    painter->drawRoundedRect(QRectF{card.left(), card.top(), 3.0, card.height()}, 1.5, 1.5);

    qreal textY = card.top() + paddingY + metrics.ascent();
    for (int i = 0; i < lines.size(); ++i) {
        if (i == 0) {
            QFont titleFont = font;
            titleFont.setBold(true);
            painter->setFont(titleFont);
            painter->setPen(axisTextColor());
        } else {
            painter->setFont(font);
            painter->setPen(mutedTextColor());
        }
        painter->drawText(QPointF{card.left() + paddingX, textY}, lines[i]);
        textY += metrics.height();
    }
    painter->restore();
}

void renderReferenceOverlays(QPainter* painter, const RenderSnapshot& snap, const HoverInfo& hover) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    auto collect = [&](const auto& rows, auto priceOf) {
        std::vector<QPointF> points;
        points.reserve(rows.size());
        for (const auto& row : rows) {
            const auto price = priceOf(row);
            if (price <= 0 || row.tsNs < snap.vp.tMin || row.tsNs > snap.vp.tMax) continue;
            if (price < snap.vp.pMin || price > snap.vp.pMax) continue;
            points.push_back(QPointF{std::round(snap.vp.toX(row.tsNs)), std::round(snap.vp.toY(price))});
        }
        return points;
    };
    if (snap.markPriceVisible) {
        drawStepLine(painter, collect(snap.markPrices, [](const hftrec::replay::MarkPriceRow& row) { return row.markPriceE8; }), QColor{255, 152, 0});
    }
    if (snap.indexPriceVisible) {
        drawStepLine(painter, collect(snap.indexPrices, [](const hftrec::replay::IndexPriceRow& row) { return row.indexPriceE8; }), QColor{53, 208, 111});
    }
    if (snap.priceLimitVisible) {
        drawStepLine(painter, collect(snap.priceLimits, [](const hftrec::replay::PriceLimitRow& row) { return row.buyLimitE8; }), QColor{255, 214, 51});
        drawStepLine(painter, collect(snap.priceLimits, [](const hftrec::replay::PriceLimitRow& row) { return row.sellLimitE8; }), QColor{255, 214, 51});
    }
    renderFundingStrip(painter, snap, hover, true);
    painter->restore();
}

void paintSnapshotLayers(QPainter* painter,
                         const RenderSnapshot& snap,
                         bool drawBackground,
                         bool drawOrderbook,
                         bool drawBookTicker,
                         bool drawTrades,
                         bool drawOverlay,
                         const HoverInfo& hover,
                         double dpr) {
    RenderSnapshot layerSnap = snap;
    layerSnap.orderbookVisible = drawOrderbook && snap.orderbookVisible;
    layerSnap.bookTickerVisible = drawBookTicker && snap.bookTickerVisible;
    layerSnap.tradesVisible = drawTrades && snap.tradesVisible;
    layerSnap.liquidationsVisible = drawTrades && snap.liquidationsVisible;
    layerSnap.candlesVisible = snap.candlesVisible;
    layerSnap.tradeConnectorsVisible = drawTrades && snap.tradeConnectorsVisible;
    if (drawBackground && !layerSnap.overlayOnly) {
        painter->fillRect(QRectF{0.0, 0.0, layerSnap.vp.w, layerSnap.vp.h}, bgColor());
    }
    if (!layerSnap.loaded) return;
    if (layerSnap.vp.tMax <= layerSnap.vp.tMin || layerSnap.vp.pMax <= layerSnap.vp.pMin) return;

    RenderContext ctx{painter, layerSnap, drawOverlay ? hover : HoverInfo{}, dpr};
    if (layerSnap.orderbookVisible) renderers::renderBook(ctx);
    if (layerSnap.bookTickerVisible) renderers::renderBookTicker(ctx);
    if (layerSnap.candlesVisible) renderers::renderCandles(ctx);
    renderReferenceOverlays(painter, layerSnap, drawOverlay ? hover : HoverInfo{});
    if (layerSnap.tradesVisible || layerSnap.liquidationsVisible) renderers::renderTrades(ctx);
    if (drawOverlay) renderers::renderOverlay(ctx);
}

QRectF sourceRectForViewport(const ViewportMap& cachedVp,
                             qint64 tsMin,
                             qint64 tsMax,
                             qint64 priceMinE8,
                             qint64 priceMaxE8) {
    const qreal x0 = cachedVp.toX(tsMin);
    const qreal x1 = cachedVp.toX(tsMax);
    const qreal y0 = cachedVp.toY(priceMaxE8);
    const qreal y1 = cachedVp.toY(priceMinE8);
    return QRectF{
        std::min(x0, x1),
        std::min(y0, y1),
        std::abs(x1 - x0),
        std::abs(y1 - y0),
    };
}

RenderSnapshot baseSnapshotForCache(const RenderSnapshot& snap) {
    return snap;
}

int nextTradeOrigIndex(const RenderSnapshot& snap) noexcept {
    if (snap.tradeDots.empty()) return 0;
    const int lastOrigIndex = snap.tradeDots.back().lastOrigIndex >= 0
        ? snap.tradeDots.back().lastOrigIndex
        : snap.tradeDots.back().origIndex;
    if (lastOrigIndex >= std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return lastOrigIndex + 1;
}

std::int64_t maxTradeTs(const RenderSnapshot& snap) noexcept {
    if (snap.tradeDots.empty()) return 0;
    const auto& last = snap.tradeDots.back();
    return last.tsEndNs != 0 ? last.tsEndNs : last.tsNs;
}

std::int64_t maxOrderbookTs(const RenderSnapshot& snap) noexcept {
    return snap.bookSegments.empty() ? 0 : snap.bookSegments.back().tsEndNs;
}

std::int64_t maxBookTickerTs(const RenderSnapshot& snap) noexcept {
    return snap.bookTickerTrace.samples.empty() ? 0 : snap.bookTickerTrace.samples.back().tsNs;
}

std::int64_t maxSnapshotTs(const LiveDataBatch& batch) noexcept {
    return batch.snapshots.empty() ? 0 : batch.snapshots.back().tsNs;
}

std::int64_t latestBookStateTs(const LiveDataBatch& batch) noexcept {
    return std::max(
        batch.depths.empty() ? 0 : batch.depths.back().tsNs,
        maxSnapshotTs(batch));
}


constexpr std::size_t kLiveRenderBookLevelsBudgetPerSide = 96;
constexpr std::size_t kLiveInteractiveBookLevelsBudgetPerSide = 48;
constexpr std::int64_t kUsdScaleE8 = 100000000ll;

std::size_t liveBookLevelCandidateBudget(std::size_t levelsBudget) noexcept {
    return std::max<std::size_t>(levelsBudget * 8u, levelsBudget);
}

std::int64_t usdToE8(qreal usd) noexcept {
    const qreal clamped = std::clamp<qreal>(usd, 1000.0, 1000000.0);
    return static_cast<std::int64_t>(std::llround(clamped * static_cast<qreal>(kUsdScaleE8)));
}

std::int64_t usdToE8Min0(qreal usd) noexcept {
    const qreal clamped = std::clamp<qreal>(usd, 0.0, 1000000.0);
    return static_cast<std::int64_t>(std::llround(clamped * static_cast<qreal>(kUsdScaleE8)));
}

qreal normalizedBrightness(std::int64_t amountE8,
                           std::int64_t minVisibleAmountE8,
                           std::int64_t brightnessRefE8) noexcept {
    if (brightnessRefE8 <= 0 || amountE8 <= 0) return 0.0;

    const std::int64_t fullBrightAmountE8 = std::max(brightnessRefE8, minVisibleAmountE8 + 1);
    const std::int64_t spanE8 = std::max<std::int64_t>(1, fullBrightAmountE8 - minVisibleAmountE8);
    const std::int64_t shiftedAmountE8 = std::max<std::int64_t>(0, amountE8 - minVisibleAmountE8);
    const qreal ratio = std::clamp(
        static_cast<qreal>(shiftedAmountE8) / static_cast<qreal>(spanE8), 0.0, 1.0);
    return std::pow(ratio, 0.28);
}

std::int64_t windowBidMinE8(std::int64_t bestBidE8, qreal windowPct) noexcept {
    if (bestBidE8 <= 0) return 0;
    const qreal clampedPct = std::clamp<qreal>(windowPct, 1.0, 25.0);
    const long double factor = 1.0L - (static_cast<long double>(clampedPct) / 100.0L);
    return static_cast<std::int64_t>(std::floor(static_cast<long double>(bestBidE8) * factor));
}

std::int64_t windowAskMaxE8(std::int64_t bestAskE8, qreal windowPct) noexcept {
    if (bestAskE8 <= 0) return 0;
    const qreal clampedPct = std::clamp<qreal>(windowPct, 1.0, 25.0);
    const long double factor = 1.0L + (static_cast<long double>(clampedPct) / 100.0L);
    return static_cast<std::int64_t>(std::ceil(static_cast<long double>(bestAskE8) * factor));
}

bool prepareVisibleLevelForScreen(const BookLevel& level,
                                  const ViewportMap& vp,
                                  std::int64_t minVisibleAmountE8,
                                  std::int64_t brightnessRefE8,
                                  int& outYPx,
                                  std::uint8_t& outAlpha) noexcept {
    if (level.qtyE8 <= 0) return false;
    if (level.priceE8 < vp.pMin || level.priceE8 > vp.pMax) return false;

    const int heightPx = static_cast<int>(std::ceil(vp.h));
    if (heightPx <= 0) return false;

    const auto y = static_cast<int>(std::round(vp.toY(level.priceE8)));
    if (y < 0 || y >= heightPx) return false;

    const auto amountE8 = detail::multiplyScaledE8(level.qtyE8, level.priceE8);
    if (amountE8 < minVisibleAmountE8) return false;

    const qreal brightness = normalizedBrightness(amountE8, minVisibleAmountE8, brightnessRefE8);
    const int alpha = std::clamp(static_cast<int>(std::round(brightness * 255.0)), 0, 255);
    if (alpha <= 1) return false;

    outYPx = y;
    outAlpha = static_cast<std::uint8_t>(alpha);
    return true;
}

template <typename BookMap>
void appendVisibleLiveLevels(const BookMap& levels,
                             std::vector<BookLevel>& out,
                             std::int64_t limitMinPriceE8,
                             std::int64_t limitMaxPriceE8,
                             const ViewportMap& vp,
                             std::int64_t minVisibleAmountE8,
                             std::int64_t brightnessRefE8,
                             std::size_t levelsBudget,
                             std::int64_t& outMaxQty) {
    int lastYPx = std::numeric_limits<int>::min();
    std::size_t kept = 0;
    for (const auto& [price, qty] : levels) {
        if (kept >= levelsBudget) break;
        if (limitMinPriceE8 > 0 && price < limitMinPriceE8) break;
        if (limitMaxPriceE8 > 0 && price > limitMaxPriceE8) break;

        int yPx = 0;
        std::uint8_t alpha = 0;
        if (!prepareVisibleLevelForScreen(
                BookLevel{price, qty},
                vp,
                minVisibleAmountE8,
                brightnessRefE8,
                yPx,
                alpha)) {
            continue;
        }
        if (yPx == lastYPx) continue;
        out.push_back(BookLevel{price, qty, alpha});
        outMaxQty = std::max(outMaxQty, qty);
        lastYPx = yPx;
        ++kept;
    }
}

template <typename Row>
bool eventKeyLess(const Row& lhs, const Row& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if constexpr (requires { lhs.captureSeq; lhs.ingestSeq; }) {
        if (lhs.captureSeq != rhs.captureSeq) return lhs.captureSeq < rhs.captureSeq;
        return lhs.ingestSeq < rhs.ingestSeq;
    } else {
        return false;
    }
}

template <typename Row>
bool sameEventKey(const Row& lhs, const Row& rhs) noexcept {
    return !eventKeyLess(lhs, rhs) && !eventKeyLess(rhs, lhs);
}

struct LiveBookEventRef {
    const hftrec::replay::SnapshotDocument* snapshot{nullptr};
    const hftrec::replay::DepthRow* depth{nullptr};
    const hftrec::replay::BookTickerRow* bookTicker{nullptr};
};

struct BookTickerWindowAnchor {
    std::int64_t bidPriceE8{0};
    std::int64_t askPriceE8{0};
};

std::int64_t liveBookEventTs(const LiveBookEventRef& event) noexcept {
    if (event.snapshot != nullptr) return event.snapshot->tsNs;
    if (event.depth != nullptr) return event.depth->tsNs;
    return event.bookTicker != nullptr ? event.bookTicker->tsNs : 0;
}

std::int64_t liveBookEventCaptureSeq(const LiveBookEventRef& event) noexcept {
    return event.bookTicker != nullptr ? event.bookTicker->captureSeq : 0;
}

std::int64_t liveBookEventIngestSeq(const LiveBookEventRef& event) noexcept {
    return event.bookTicker != nullptr ? event.bookTicker->ingestSeq : 0;
}

int liveBookEventKindOrder(const LiveBookEventRef& event) noexcept {
    if (event.snapshot != nullptr) return 0;
    if (event.depth != nullptr) return 1;
    if (event.bookTicker != nullptr) return 2;
    return 3;
}

bool liveBookEventLess(const LiveBookEventRef& lhs, const LiveBookEventRef& rhs) noexcept {
    const auto lhsTs = liveBookEventTs(lhs);
    const auto rhsTs = liveBookEventTs(rhs);
    if (lhsTs != rhsTs) return lhsTs < rhsTs;
    const auto lhsCaptureSeq = liveBookEventCaptureSeq(lhs);
    const auto rhsCaptureSeq = liveBookEventCaptureSeq(rhs);
    if (lhsCaptureSeq != rhsCaptureSeq) return lhsCaptureSeq < rhsCaptureSeq;
    const auto lhsIngestSeq = liveBookEventIngestSeq(lhs);
    const auto rhsIngestSeq = liveBookEventIngestSeq(rhs);
    if (lhsIngestSeq != rhsIngestSeq) return lhsIngestSeq < rhsIngestSeq;
    return liveBookEventKindOrder(lhs) < liveBookEventKindOrder(rhs);
}

void absorbBookTickerAnchor(BookTickerWindowAnchor& anchor,
                            const hftrec::replay::BookTickerRow& ticker) noexcept {
    if (ticker.bidPriceE8 > 0) anchor.bidPriceE8 = ticker.bidPriceE8;
    if (ticker.askPriceE8 > 0) anchor.askPriceE8 = ticker.askPriceE8;
}

void absorbBookTickerAnchorAtOrBefore(BookTickerWindowAnchor& anchor,
                                      std::int64_t& anchorTsNs,
                                      const std::vector<hftrec::replay::BookTickerRow>& rows,
                                      std::int64_t tsNs) noexcept {
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        if (it->tsNs > tsNs) continue;
        if (it->bidPriceE8 <= 0 && it->askPriceE8 <= 0) continue;
        if (it->tsNs >= anchorTsNs) {
            absorbBookTickerAnchor(anchor, *it);
            anchorTsNs = it->tsNs;
        }
        return;
    }
}

void appendLiveBookSegment(std::vector<BookSegment>& out,
                           const ViewportMap& vp,
                           const RenderSnapshot& live,
                           const hftrec::replay::BookState& state,
                           const BookTickerWindowAnchor& anchor,
                           std::int64_t tsStart,
                           std::int64_t tsEnd) {
    if (tsEnd <= tsStart) return;
    if (state.empty()) return;

    BookSegment seg{};
    seg.tsStartNs = tsStart;
    seg.tsEndNs = tsEnd;

    const std::int64_t brightnessRefE8 = usdToE8(live.bookOpacityGain);
    const std::int64_t minVisibleAmountE8 = usdToE8Min0(live.bookRenderDetail);
    const std::size_t levelsBudget = live.interactiveMode
        ? kLiveInteractiveBookLevelsBudgetPerSide
        : kLiveRenderBookLevelsBudgetPerSide;
    const std::int64_t anchorBidE8 = anchor.bidPriceE8 > 0 ? anchor.bidPriceE8 : state.bestBidPrice();
    const std::int64_t anchorAskE8 = anchor.askPriceE8 > 0 ? anchor.askPriceE8 : state.bestAskPrice();
    const std::int64_t bidMinE8 = windowBidMinE8(anchorBidE8, live.bookDepthWindowPct);
    const std::int64_t askMaxE8 = windowAskMaxE8(anchorAskE8, live.bookDepthWindowPct);
    const std::size_t candidateBudget = liveBookLevelCandidateBudget(levelsBudget);
    const std::int64_t bidFilterMinE8 = std::max<std::int64_t>(vp.pMin, bidMinE8);
    const std::int64_t askFilterMaxE8 = askMaxE8 > 0
        ? std::min<std::int64_t>(vp.pMax, askMaxE8)
        : vp.pMax;
    const auto bidLevels = state.filteredBids(bidFilterMinE8, vp.pMax, candidateBudget);
    const auto askLevels = state.filteredAsks(vp.pMin, askFilterMaxE8, candidateBudget);
    std::int64_t maxBid = 0;
    std::int64_t maxAsk = 0;

    appendVisibleLiveLevels(
        bidLevels,
        seg.bids,
        bidMinE8,
        0,
        vp,
        minVisibleAmountE8,
        brightnessRefE8,
        levelsBudget,
        maxBid);
    appendVisibleLiveLevels(
        askLevels,
        seg.asks,
        0,
        askMaxE8,
        vp,
        minVisibleAmountE8,
        brightnessRefE8,
        levelsBudget,
        maxAsk);

    if (seg.bids.empty() && seg.asks.empty()) return;
    seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
    seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
    out.push_back(std::move(seg));
}

void buildLiveOrderbookSegments(RenderSnapshot& live, const LiveDataCache& cache) {
    if (!live.orderbookVisible) return;

    const std::int64_t renderTsMin = cache.hasRenderRange
        ? std::max<std::int64_t>(live.vp.tMin, cache.renderTsMin)
        : live.vp.tMin;
    const std::int64_t renderTsMax = cache.hasRenderRange
        ? std::min<std::int64_t>(live.vp.tMax, cache.renderTsMax)
        : live.vp.tMax;
    if (renderTsMax <= renderTsMin) return;

    const std::int64_t liveVisibleTsMax = std::min<std::int64_t>(
        renderTsMax,
        std::max(latestBookStateTs(cache.stableRows), latestBookStateTs(cache.overlayRows)));
    if (liveVisibleTsMax <= renderTsMin) return;

    std::vector<LiveBookEventRef> events;
    events.reserve(cache.stableRows.snapshots.size() + cache.stableRows.depths.size()
                   + cache.overlayRows.snapshots.size() + cache.overlayRows.depths.size());
    for (const auto& snapshot : cache.stableRows.snapshots) events.push_back(LiveBookEventRef{&snapshot, nullptr});
    for (const auto& depth : cache.stableRows.depths) events.push_back(LiveBookEventRef{nullptr, &depth});
    for (const auto& snapshot : cache.overlayRows.snapshots) {
        const bool duplicate = std::any_of(
            cache.stableRows.snapshots.begin(),
            cache.stableRows.snapshots.end(),
            [&snapshot](const hftrec::replay::SnapshotDocument& stable) noexcept {
                return sameEventKey(stable, snapshot);
            });
        if (!duplicate) events.push_back(LiveBookEventRef{&snapshot, nullptr});
    }
    for (const auto& depth : cache.overlayRows.depths) {
        const bool duplicate = std::any_of(
            cache.stableRows.depths.begin(),
            cache.stableRows.depths.end(),
            [&depth](const hftrec::replay::DepthRow& stable) noexcept {
                return sameEventKey(stable, depth);
            });
        if (!duplicate) events.push_back(LiveBookEventRef{nullptr, &depth});
    }
    if (events.empty()) return;

    std::sort(events.begin(), events.end(), liveBookEventLess);

    hftrec::replay::BookState state{};
    BookTickerWindowAnchor anchor{};
    std::int64_t anchorTsNs = 0;
    absorbBookTickerAnchorAtOrBefore(anchor, anchorTsNs, cache.stableRows.bookTickers, renderTsMin);
    absorbBookTickerAnchorAtOrBefore(anchor, anchorTsNs, cache.overlayRows.bookTickers, renderTsMin);
    bool hasState = false;
    std::int64_t segmentStartTs = renderTsMin;

    for (const auto& event : events) {
        const std::int64_t eventTs = liveBookEventTs(event);
        if (eventTs > liveVisibleTsMax) break;
        if (hasState && eventTs > segmentStartTs) {
            appendLiveBookSegment(live.bookSegments, live.vp, live, state, anchor, segmentStartTs, std::min(eventTs, liveVisibleTsMax));
        }

        if (event.snapshot != nullptr) state.applySnapshot(*event.snapshot);
        else if (event.depth != nullptr) state.applyDelta(*event.depth);
        else if (event.bookTicker != nullptr) absorbBookTickerAnchor(anchor, *event.bookTicker);

        hasState = !state.empty();
        segmentStartTs = std::max<std::int64_t>(renderTsMin, eventTs);
        if (segmentStartTs >= liveVisibleTsMax) break;
    }

    if (hasState && segmentStartTs < liveVisibleTsMax) {
        appendLiveBookSegment(live.bookSegments, live.vp, live, state, anchor, segmentStartTs, liveVisibleTsMax);
    }
}

void buildLiveBookTickerTrace(BookTickerTrace& trace,
                              const ViewportMap& vp,
                              const std::vector<const hftrec::replay::BookTickerRow*>& rows) {
    trace = BookTickerTrace{};
    if (rows.empty() || vp.w <= 0.0 || vp.h <= 0.0) return;
    trace.samples.reserve(std::max<std::size_t>(static_cast<std::size_t>(vp.w / 4.0) + 2u, rows.size()));
    trace.bidLines.reserve((rows.size() + 1u) * 2u);
    trace.askLines.reserve(trace.bidLines.capacity());

    detail::BookTickerTraceBuildState state{};
    const hftrec::replay::BookTickerRow* carry = nullptr;
    std::size_t firstIndex = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = *rows[i];
        if (row.tsNs > vp.tMax) break;
        if (row.tsNs < vp.tMin) {
            if (row.bidPriceE8 > 0 || row.askPriceE8 > 0) carry = &row;
            continue;
        }
        firstIndex = i;
        break;
    }

    if (carry != nullptr) {
        const std::int64_t endTs = firstIndex < rows.size()
            ? std::min<std::int64_t>(rows[firstIndex]->tsNs, vp.tMax)
            : vp.tMax;
        detail::appendBookTickerTraceSegment(trace, state, vp, *carry, vp.tMin, endTs, true, false);
    }

    for (std::size_t i = firstIndex; i < rows.size(); ++i) {
        const auto& row = *rows[i];
        if (row.tsNs > vp.tMax) break;
        const std::int64_t endTs = (i + 1u < rows.size())
            ? std::min<std::int64_t>(rows[i + 1u]->tsNs, vp.tMax)
            : vp.tMax;
        const bool hasNextAtEnd = i + 1u < rows.size() && rows[i + 1u]->tsNs <= vp.tMax;
        detail::appendBookTickerTraceSegment(trace,
                                             state,
                                             vp,
                                             row,
                                             std::max<std::int64_t>(row.tsNs, vp.tMin),
                                             endTs,
                                             true,
                                             !hasNextAtEnd);
    }
}

RenderSnapshot liveSnapshotFromDataBatch(const RenderSnapshot& base,
                                         const LiveDataCache& cache,
                                         int tradeOrigIndexStart) {
    RenderSnapshot live = base;
    live.bookSegments.clear();
    live.bookTickerTrace = BookTickerTrace{};
    live.tradeDots.clear();
    live.candleRects.clear();
    live.liquidationDots.clear();
    live.gpuBookVertices.clear();
    live.tradeDecimated = false;
    live.tradeConnectorsVisible = live.tradesVisible;

    if (!live.loaded || live.vp.tMax <= live.vp.tMin || live.vp.pMax <= live.vp.pMin) return live;

    int tradeOrigIndex = tradeOrigIndexStart;
    auto appendExactTradeRows = [&](const auto& rows) {
        for (const auto& row : rows) {
            const int rowOrigIndex = tradeOrigIndex;
            if (tradeOrigIndex < std::numeric_limits<int>::max()) ++tradeOrigIndex;
            if (row.tsNs < live.vp.tMin || row.tsNs > live.vp.tMax) continue;
            if (row.priceE8 < live.vp.pMin || row.priceE8 > live.vp.pMax) continue;
            detail::appendGroupedTradeDot(live.tradeDots, TradeDot{row.tsNs, row.priceE8, row.qtyE8, row.sideBuy != 0, rowOrigIndex});
        }
    };
    if (live.tradesVisible) {
        appendExactTradeRows(cache.stableRows.trades);
        appendExactTradeRows(cache.overlayRows.trades);
    }

    if (live.liquidationsVisible) {
        int liquidationOrigIndex = 0;
        const auto appendLiquidationRows = [&](const auto& rows) {
            for (const auto& row : rows) {
                const int rowOrigIndex = liquidationOrigIndex;
                if (liquidationOrigIndex < std::numeric_limits<int>::max()) ++liquidationOrigIndex;
                if (row.tsNs < live.vp.tMin || row.tsNs > live.vp.tMax) continue;
                if (row.priceE8 < live.vp.pMin || row.priceE8 > live.vp.pMax) continue;
                live.liquidationDots.push_back(LiquidationDot{row.tsNs, row.priceE8, row.qtyE8, row.avgPriceE8, row.filledQtyE8, row.sideBuy != 0, rowOrigIndex});
            }
        };
        appendLiquidationRows(cache.stableRows.liquidations);
        appendLiquidationRows(cache.overlayRows.liquidations);
    }

    if (live.bookTickerVisible) {
        std::vector<const hftrec::replay::BookTickerRow*> rows;
        rows.reserve(cache.stableRows.bookTickers.size() + cache.overlayRows.bookTickers.size());
        for (const auto& row : cache.stableRows.bookTickers) rows.push_back(&row);
        for (const auto& row : cache.overlayRows.bookTickers) rows.push_back(&row);
        if (!rows.empty()) {
            std::int64_t currentTs = 0;
            for (const auto* row : rows) currentTs = std::max(currentTs, row->tsNs);
            ViewportMap tickerVp = live.vp;
            tickerVp.tMax = std::min<std::int64_t>(tickerVp.tMax, currentTs);
            if (tickerVp.tMax >= tickerVp.tMin) buildLiveBookTickerTrace(live.bookTickerTrace, tickerVp, rows);
        }
    }

    buildLiveOrderbookSegments(live, cache);

    return live;
}

void recordGuiObjectCounts(const RenderSnapshot& base, const RenderSnapshot* live = nullptr) {
    std::uint64_t orderbookSegments = static_cast<std::uint64_t>(base.bookSegments.size());
    std::uint64_t bookTickerLines = static_cast<std::uint64_t>(base.bookTickerTrace.bidLines.size()
        + base.bookTickerTrace.askLines.size());
    std::uint64_t bookTickerSamples = static_cast<std::uint64_t>(base.bookTickerTrace.samples.size());
    std::uint64_t tradeDots = static_cast<std::uint64_t>(base.tradeDots.size());
    std::uint64_t liquidationDots = static_cast<std::uint64_t>(base.liquidationDots.size());
    if (live != nullptr) {
        orderbookSegments += static_cast<std::uint64_t>(live->bookSegments.size());
        bookTickerLines += static_cast<std::uint64_t>(live->bookTickerTrace.bidLines.size()
            + live->bookTickerTrace.askLines.size());
        bookTickerSamples += static_cast<std::uint64_t>(live->bookTickerTrace.samples.size());
        tradeDots += static_cast<std::uint64_t>(live->tradeDots.size());
        liquidationDots += static_cast<std::uint64_t>(live->liquidationDots.size());
    }
    metrics::setGuiObjectCounts(orderbookSegments, bookTickerLines, bookTickerSamples, tradeDots, liquidationDots);
}
void appendSnapshotRows(RenderSnapshot& target, RenderSnapshot&& rows) {
    target.bookSegments.insert(
        target.bookSegments.end(),
        std::make_move_iterator(rows.bookSegments.begin()),
        std::make_move_iterator(rows.bookSegments.end()));
    target.bookTickerTrace.bidLines.insert(
        target.bookTickerTrace.bidLines.end(),
        std::make_move_iterator(rows.bookTickerTrace.bidLines.begin()),
        std::make_move_iterator(rows.bookTickerTrace.bidLines.end()));
    target.bookTickerTrace.askLines.insert(
        target.bookTickerTrace.askLines.end(),
        std::make_move_iterator(rows.bookTickerTrace.askLines.begin()),
        std::make_move_iterator(rows.bookTickerTrace.askLines.end()));
    target.bookTickerTrace.samples.insert(
        target.bookTickerTrace.samples.end(),
        std::make_move_iterator(rows.bookTickerTrace.samples.begin()),
        std::make_move_iterator(rows.bookTickerTrace.samples.end()));
    target.tradeDots.insert(
        target.tradeDots.end(),
        std::make_move_iterator(rows.tradeDots.begin()),
        std::make_move_iterator(rows.tradeDots.end()));
    target.candleRects.insert(
        target.candleRects.end(),
        std::make_move_iterator(rows.candleRects.begin()),
        std::make_move_iterator(rows.candleRects.end()));
    target.liquidationDots.insert(
        target.liquidationDots.end(),
        std::make_move_iterator(rows.liquidationDots.begin()),
        std::make_move_iterator(rows.liquidationDots.end()));
}

void drawTradeBridge(QPainter* painter, const RenderSnapshot& base, const RenderSnapshot& live) {
    if (!base.tradesVisible || !base.tradeConnectorsVisible || !live.tradeConnectorsVisible || base.tradeDots.empty() || live.tradeDots.empty()) return;
    const auto& prev = base.tradeDots.back();
    const auto& last = live.tradeDots.front();
    const int prevLastOrig = prev.lastOrigIndex >= 0 ? prev.lastOrigIndex : prev.origIndex;
    const int lastFirstOrig = last.firstOrigIndex >= 0 ? last.firstOrigIndex : last.origIndex;
    if (prevLastOrig + 1 != lastFirstOrig) return;

    const QPointF p0{base.vp.toX(prev.tsNs), base.vp.toY(prev.priceE8)};
    const QPointF p1{base.vp.toX(last.tsNs), base.vp.toY(last.priceE8)};
    if (p0 == p1) return;

    painter->save();
    QPen pen(tradeConnectorColor());
    pen.setWidth(1);
    pen.setCapStyle(Qt::SquareCap);
    painter->setPen(pen);
    painter->drawLine(p0, p1);
    painter->restore();
}

void paintLiveSnapshot(QPainter* painter,
                       const RenderSnapshot& base,
                       const RenderSnapshot& live,
                       double dpr) {
    if (!live.loaded) return;
    RenderContext ctx{painter, live, HoverInfo{}, dpr};
    renderers::renderBook(ctx);
    renderers::renderBookTicker(ctx);
    drawTradeBridge(painter, base, live);
    renderers::renderTrades(ctx);
}

}  // namespace

void ChartItem::requestRepaint() {
    if (interactiveMode_ && cachedExactSnap_) {
        cachedInteractiveSnap_.reset();
        interactiveDirty_ = true;
    } else {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
    if (hoverActive_ && !interactiveMode_) updateHover_();
    update();
}

void ChartItem::requestLiveRepaint() {
    cachedLiveSnap_.reset();
    cachedLiveDataBatchId_ = 0;
    if (!contextActive_) {
        cachedHitTestSnap_.reset();
        cachedHitTestBatchId_ = 0;
        if (hoverActive_ && !interactiveMode_) updateHover_();
    }
    update();
}

void ChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
}

void ChartItem::invalidateSnapshotCache_() {
    cachedInteractiveSnap_.reset();
    cachedExactSnap_.reset();
    cachedHitTestSnap_.reset();
    cachedHitTestBatchId_ = 0;
    interactiveDirty_ = false;
    exactDirty_ = false;
}

void ChartItem::invalidateBaseImage_() {
    cachedOrderbookImage_ = QImage{};
    cachedBookTickerImage_ = QImage{};
    cachedTradesImage_ = QImage{};
    cachedLayerImageW_ = 0.0;
    cachedLayerImageH_ = 0.0;
    cachedOrderbookEndTsNs_ = 0;
    cachedBookTickerEndTsNs_ = 0;
    cachedTradesEndTsNs_ = 0;
    cachedLiveDataBatchId_ = 0;
    cachedHitTestBatchId_ = 0;
    cachedLiveSnap_.reset();
    cachedHitTestSnap_.reset();
}

void ChartItem::invalidateOrderbookImage_() {
    cachedOrderbookImage_ = QImage{};
    cachedOrderbookEndTsNs_ = 0;
    cachedLiveDataBatchId_ = 0;
    cachedHitTestBatchId_ = 0;
    cachedLiveSnap_.reset();
    cachedHitTestSnap_.reset();
}

void ChartItem::invalidateTradesImage_() {
    cachedTradesImage_ = QImage{};
    cachedTradesEndTsNs_ = 0;
    cachedLiveDataBatchId_ = 0;
    cachedHitTestBatchId_ = 0;
    cachedLiveSnap_.reset();
    cachedHitTestSnap_.reset();
}

std::unique_ptr<RenderSnapshot>& ChartItem::activeSnapshotCache_() noexcept {
    return interactiveMode_ ? cachedInteractiveSnap_ : cachedExactSnap_;
}

void ChartItem::mergeLiveSnapshotIntoBaseImage_() {
    if (!cachedLiveSnap_ || cachedLiveSnap_->overlayOnly) return;
    if (cachedLayerImageW_ <= 0.0 || cachedLayerImageH_ <= 0.0) return;

    if (!cachedOrderbookImage_.isNull()) {
        QPainter painter(&cachedOrderbookImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveBook = *cachedLiveSnap_;
        liveBook.tradesVisible = false;
        liveBook.candlesVisible = false;
        liveBook.bookTickerVisible = false;
        RenderContext ctx{&painter, liveBook, HoverInfo{}, 1.0};
        renderers::renderBook(ctx);
        painter.end();
    }

    if (!cachedBookTickerImage_.isNull()) {
        QPainter painter(&cachedBookTickerImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveTicker = *cachedLiveSnap_;
        liveTicker.tradesVisible = false;
        liveTicker.candlesVisible = false;
        liveTicker.orderbookVisible = false;
        RenderContext ctx{&painter, liveTicker, HoverInfo{}, 1.0};
        renderers::renderBookTicker(ctx);
        painter.end();
    }

    if (!cachedTradesImage_.isNull()) {
        QPainter painter(&cachedTradesImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveTrades = *cachedLiveSnap_;
        liveTrades.orderbookVisible = false;
        liveTrades.bookTickerVisible = false;
        const RenderSnapshot base = (cachedExactSnap_ && controller_ != nullptr)
            ? baseSnapshotForCache(*cachedExactSnap_)
            : RenderSnapshot{};
        drawTradeBridge(&painter, base, liveTrades);
        RenderContext ctx{&painter, liveTrades, HoverInfo{}, 1.0};
        renderers::renderTrades(ctx);
        painter.end();
    }
    cachedOrderbookEndTsNs_ = std::max(cachedOrderbookEndTsNs_, maxOrderbookTs(*cachedLiveSnap_));
    cachedBookTickerEndTsNs_ = std::max(cachedBookTickerEndTsNs_, maxBookTickerTs(*cachedLiveSnap_));
    cachedTradesEndTsNs_ = std::max(cachedTradesEndTsNs_, maxTradeTs(*cachedLiveSnap_));
}

const RenderSnapshot& ChartItem::ensureSnapshot_() {
    const qreal w = width();
    const qreal h = height();
    auto& activeCache = activeSnapshotCache_();
    bool& activeDirty = interactiveMode_ ? interactiveDirty_ : exactDirty_;
    const bool sizeChanged = (cachedW_ != w || cachedH_ != h);
    const bool rebuildActive = activeDirty || !activeCache || sizeChanged;

    if (rebuildActive) {
        const hftrec::timing::Tick snapshotBuildStart = hftrec::timing::captureTick();
        activeCache = std::make_unique<RenderSnapshot>(controller_->buildSnapshot(w, h, detail::collectInputs(*this)));
        metrics::recordGuiSnapshotBuild(hftrec::timing::deltaNs(snapshotBuildStart, hftrec::timing::captureTick()).raw);
        cachedW_ = w;
        cachedH_ = h;
        activeDirty = false;
    }
    return *activeCache;
}

void ChartItem::ensureLayerImages_(const RenderSnapshot& snap, qreal w, qreal h) {
    if (overlayOnly_) return;
    if (!snap.loaded) return;
    recordGuiObjectCounts(snap);
    const bool sizeMatches = (cachedLayerImageW_ == w && cachedLayerImageH_ == h);
    if (!sizeMatches) {
        cachedOrderbookImage_ = QImage{};
        cachedBookTickerImage_ = QImage{};
        cachedTradesImage_ = QImage{};
        cachedOrderbookEndTsNs_ = 0;
        cachedBookTickerEndTsNs_ = 0;
        cachedTradesEndTsNs_ = 0;
    }
    if (!cachedOrderbookImage_.isNull() && !cachedBookTickerImage_.isNull() && !cachedTradesImage_.isNull()) {
        metrics::incGuiLayerCacheHit();
        return;
    }
    metrics::incGuiLayerCacheRebuild();

    const RenderSnapshot baseSnap = baseSnapshotForCache(snap);
    const std::int64_t baseOrderbookEndTsNs = maxOrderbookTs(baseSnap);
    const std::int64_t baseBookTickerEndTsNs = maxBookTickerTs(baseSnap);
    const std::int64_t baseTradesEndTsNs = maxTradeTs(baseSnap);

    const int imageW = std::max(1, static_cast<int>(std::ceil(w)));
    const int imageH = std::max(1, static_cast<int>(std::ceil(h)));

    if (cachedOrderbookImage_.isNull()) {
        QImage orderbookImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        orderbookImage.fill(bgColor().rgba());
        QPainter painter(&orderbookImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, true, false, false, false, HoverInfo{}, 1.0);
        painter.end();
        cachedOrderbookImage_ = std::move(orderbookImage);
    }

    if (cachedBookTickerImage_.isNull()) {
        QImage bookTickerImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        bookTickerImage.fill(Qt::transparent);
        QPainter painter(&bookTickerImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, false, true, false, false, HoverInfo{}, 1.0);
        painter.end();
        cachedBookTickerImage_ = std::move(bookTickerImage);
    }

    if (cachedTradesImage_.isNull()) {
        QImage tradesImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        tradesImage.fill(Qt::transparent);
        QPainter painter(&tradesImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, false, false, true, false, HoverInfo{}, 1.0);
        painter.end();
        cachedTradesImage_ = std::move(tradesImage);
    }

    cachedLayerImageW_ = w;
    cachedLayerImageH_ = h;
    cachedOrderbookEndTsNs_ = std::max(cachedOrderbookEndTsNs_, baseOrderbookEndTsNs);
    cachedBookTickerEndTsNs_ = std::max(cachedBookTickerEndTsNs_, baseBookTickerEndTsNs);
    cachedTradesEndTsNs_ = std::max(cachedTradesEndTsNs_, baseTradesEndTsNs);
}

void ChartItem::paint(QPainter* painter) {
    const hftrec::timing::Tick paintStart = hftrec::timing::captureTick();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF rect = boundingRect();
    if (!overlayOnly_) painter->fillRect(rect, bgColor());

    if (!controller_ || width() <= 0 || height() <= 0) return;

    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal w = width();
    const qreal h = height();

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) {
        paintSnapshotFrame(painter, snap, detail::buildHoverInfo(*this), dpr, false);
        return;
    }

    const RenderSnapshot& layerSnap = (interactiveMode_ && cachedExactSnap_) ? *cachedExactSnap_ : snap;
    controller_->refreshLiveDataWindow(snap.vp.tMin, snap.vp.tMax);
    ensureLayerImages_(layerSnap, w, h);
    const auto& liveCache = controller_->liveDataCache();
    const RenderSnapshot baseSnap = baseSnapshotForCache(snap);
    if (cachedLiveDataBatchId_ != liveCache.version) {
        if (controller_->renderWindowActive()) invalidateBaseImage_();
        cachedLiveDataBatchId_ = 0;
        cachedHitTestBatchId_ = 0;
        cachedLiveSnap_.reset();
        cachedHitTestSnap_.reset();
    }
    if (!cachedLiveSnap_ || cachedLiveDataBatchId_ != liveCache.version) {
        const int liveTradeOrigIndexStart = nextTradeOrigIndex(baseSnap);
        const hftrec::timing::Tick liveSnapshotStart = hftrec::timing::captureTick();
        cachedLiveSnap_ = std::make_unique<RenderSnapshot>(
            liveSnapshotFromDataBatch(snap, liveCache, liveTradeOrigIndexStart));
        metrics::recordGuiLiveSnapshotBuild(hftrec::timing::deltaNs(liveSnapshotStart, hftrec::timing::captureTick()).raw);
        cachedLiveDataBatchId_ = liveCache.version;
    }
    recordGuiObjectCounts(baseSnap, cachedLiveSnap_.get());
    const auto refreshHitTestSnapshot = [&]() {
        RenderSnapshot hitTestSnap = baseSnap;
        if (cachedLiveSnap_ != nullptr && cachedLiveSnap_->loaded) {
            RenderSnapshot liveRows = *cachedLiveSnap_;
            appendSnapshotRows(hitTestSnap, std::move(liveRows));
        }
        cachedHitTestSnap_ = std::make_unique<RenderSnapshot>(std::move(hitTestSnap));
        cachedHitTestBatchId_ = liveCache.version;
    };
    if (!cachedHitTestSnap_ || cachedHitTestBatchId_ != liveCache.version) {
        refreshHitTestSnapshot();
    }

    bool rebuildLayersForCurrentViewport = false;
    if (interactiveMode_ && !cachedOrderbookImage_.isNull() && !cachedBookTickerImage_.isNull()
        && !cachedTradesImage_.isNull() && cachedExactSnap_) {
        const QRectF sourceRect = sourceRectForViewport(
            cachedExactSnap_->vp,
            controller_->tsMin(),
            controller_->tsMax(),
            controller_->priceMinE8(),
            controller_->priceMaxE8());
        const QRectF fullSource{
            0.0,
            0.0,
            static_cast<qreal>(cachedOrderbookImage_.width()),
            static_cast<qreal>(cachedOrderbookImage_.height())
        };
        const QRectF clippedSource = sourceRect.intersected(fullSource);
        const bool cacheCoversViewport = sourceRect.width() > 0.5 && sourceRect.height() > 0.5
            && clippedSource.width() > 0.5 && clippedSource.height() > 0.5
            && std::abs(clippedSource.left() - sourceRect.left()) <= 0.5
            && std::abs(clippedSource.top() - sourceRect.top()) <= 0.5
            && std::abs(clippedSource.width() - sourceRect.width()) <= 0.5
            && std::abs(clippedSource.height() - sourceRect.height()) <= 0.5;
        if (cacheCoversViewport) {
            const QRectF destRect{
                rect.left() + ((clippedSource.left() - sourceRect.left()) / sourceRect.width()) * rect.width(),
                rect.top() + ((clippedSource.top() - sourceRect.top()) / sourceRect.height()) * rect.height(),
                (clippedSource.width() / sourceRect.width()) * rect.width(),
                (clippedSource.height() / sourceRect.height()) * rect.height(),
            };
            const hftrec::timing::Tick orderbookRenderStart = hftrec::timing::captureTick();
            painter->drawImage(destRect, cachedOrderbookImage_, clippedSource);
            metrics::recordGuiRenderOrderbook(hftrec::timing::deltaNs(orderbookRenderStart, hftrec::timing::captureTick()).raw);
            const hftrec::timing::Tick bookTickerRenderStart = hftrec::timing::captureTick();
            painter->drawImage(destRect, cachedBookTickerImage_, clippedSource);
            metrics::recordGuiRenderBookTicker(hftrec::timing::deltaNs(bookTickerRenderStart, hftrec::timing::captureTick()).raw);
            const hftrec::timing::Tick tradesRenderStart = hftrec::timing::captureTick();
            painter->drawImage(destRect, cachedTradesImage_, clippedSource);
            metrics::recordGuiRenderTrades(hftrec::timing::deltaNs(tradesRenderStart, hftrec::timing::captureTick()).raw);
            const hftrec::timing::Tick liveSnapshotDrawStart = hftrec::timing::captureTick();
            paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);
            metrics::recordGuiLiveSnapshotDraw(hftrec::timing::deltaNs(liveSnapshotDrawStart, hftrec::timing::captureTick()).raw);
            const hftrec::timing::Tick frameEnd = hftrec::timing::captureTick();
            metrics::recordGuiPaint(hftrec::timing::deltaNs(paintStart, frameEnd).raw, frameEnd.raw);
            return;
        }

        // If the user pans/zooms outside the settled snapshot, a transformed
        // blit can no longer represent the current viewport. Rebuild now;
        // otherwise the chart image would remain on the old viewport while the
        // QML scales already reflect the new controller bounds.
        rebuildLayersForCurrentViewport = true;
    }

    if (rebuildLayersForCurrentViewport) {
        invalidateBaseImage_();
        ensureLayerImages_(snap, w, h);
        if (!cachedLiveSnap_ || cachedLiveDataBatchId_ != liveCache.version) {
            const int liveTradeOrigIndexStart = nextTradeOrigIndex(baseSnap);
            cachedLiveSnap_ = std::make_unique<RenderSnapshot>(
                liveSnapshotFromDataBatch(snap, liveCache, liveTradeOrigIndexStart));
            cachedLiveDataBatchId_ = liveCache.version;
        }
        recordGuiObjectCounts(baseSnap, cachedLiveSnap_.get());
        if (!cachedHitTestSnap_ || cachedHitTestBatchId_ != liveCache.version) refreshHitTestSnapshot();
    }

    if (!cachedOrderbookImage_.isNull() && !overlayOnly_) {
        const hftrec::timing::Tick orderbookRenderStart = hftrec::timing::captureTick();
        painter->drawImage(rect, cachedOrderbookImage_);
        metrics::recordGuiRenderOrderbook(hftrec::timing::deltaNs(orderbookRenderStart, hftrec::timing::captureTick()).raw);
    }
    if (!cachedBookTickerImage_.isNull()) {
        const hftrec::timing::Tick bookTickerRenderStart = hftrec::timing::captureTick();
        painter->drawImage(rect, cachedBookTickerImage_);
        metrics::recordGuiRenderBookTicker(hftrec::timing::deltaNs(bookTickerRenderStart, hftrec::timing::captureTick()).raw);
    }
    if (!cachedTradesImage_.isNull()) {
        const hftrec::timing::Tick tradesRenderStart = hftrec::timing::captureTick();
        painter->drawImage(rect, cachedTradesImage_);
        metrics::recordGuiRenderTrades(hftrec::timing::deltaNs(tradesRenderStart, hftrec::timing::captureTick()).raw);
    }
    const hftrec::timing::Tick liveSnapshotDrawStart = hftrec::timing::captureTick();
    paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);
    metrics::recordGuiLiveSnapshotDraw(hftrec::timing::deltaNs(liveSnapshotDrawStart, hftrec::timing::captureTick()).raw);

    if (!interactiveMode_) {
        const hftrec::timing::Tick overlayRenderStart = hftrec::timing::captureTick();
        const HoverInfo hover = detail::buildHoverInfo(*this);
        renderFundingStrip(painter, snap, hover, false);
        RenderContext ctx{painter, snap, hover, dpr};
        if (detail::shouldRenderStrategyOverlayInFinalPass(snap, interactiveMode_)) {
            renderers::renderStrategyOverlay(ctx);
        }
        renderers::renderOverlay(ctx);
        metrics::recordGuiOverlayRender(hftrec::timing::deltaNs(overlayRenderStart, hftrec::timing::captureTick()).raw);
    }
    const hftrec::timing::Tick frameEnd = hftrec::timing::captureTick();
    metrics::recordGuiPaint(hftrec::timing::deltaNs(paintStart, frameEnd).raw, frameEnd.raw);
}

}  // namespace hftrec::gui::viewer




