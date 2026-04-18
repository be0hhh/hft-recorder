#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <QDateTime>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
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

qreal clampReal(qreal value, qreal lo, qreal hi) {
    return std::clamp(value, lo, hi);
}

template <typename MapT>
std::int64_t maxVisibleQty(const MapT& levels, std::int64_t priceMin, std::int64_t priceMax) {
    std::int64_t maxQty = 0;
    for (const auto& [price, qty] : levels) {
        if (price < priceMin || price > priceMax) continue;
        maxQty = std::max(maxQty, qty);
    }
    return maxQty;
}

template <typename MapT>
bool findNearestBookLevel(const MapT& levels,
                          const ViewportMap& vp,
                          qreal hoverY,
                          double maxDistancePx,
                          std::int64_t& outPriceE8,
                          std::int64_t& outQtyE8) {
    double bestDistancePx = maxDistancePx;
    bool found = false;
    for (const auto& [price, qty] : levels) {
        if (price < vp.pMin || price > vp.pMax || qty <= 0) continue;
        const double distancePx = std::abs(vp.toY(price) - hoverY);
        if (distancePx <= bestDistancePx) {
            bestDistancePx = distancePx;
            outPriceE8 = price;
            outQtyE8 = qty;
            found = true;
        }
    }
    return found;
}

qreal amountRadiusScale(std::int64_t amountE8, qreal amountScale, bool interactiveMode) {
    const auto amountAbs = amountE8 < 0
        ? static_cast<double>(static_cast<std::uint64_t>(-(amountE8 + 1)) + 1u)
        : static_cast<double>(static_cast<std::uint64_t>(amountE8));
    const double normalized = std::log10(1.0 + amountAbs / 100000000.0);
    const qreal baseRadius = interactiveMode ? 0.65 : 0.8;
    const qreal gain = interactiveMode ? 1.4 : 2.2;
    return clampReal(baseRadius + static_cast<qreal>(normalized) * amountScale * gain,
                     0.55,
                     interactiveMode ? 2.4 : 3.4);
}

void appendStepSegment(QPainterPath& path, bool& started, qreal xLeft, qreal xRight, qreal y) {
    if (xRight <= xLeft) return;
    if (!started) {
        path.moveTo(xLeft, y);
        path.lineTo(xRight, y);
        started = true;
        return;
    }

    const QPointF current = path.currentPosition();
    if (!qFuzzyCompare(current.x() + 1.0, xLeft + 1.0)) {
        path.lineTo(xLeft, current.y());
    }
    if (!qFuzzyCompare(current.y() + 1.0, y + 1.0)) {
        path.lineTo(xLeft, y);
    }
    path.lineTo(xRight, y);
}

template <typename MapT>
void drawBookSideSegment(QPainter* painter,
                         const MapT& levels,
                         const ViewportMap& vp,
                         qreal xLeft,
                         qreal xRight,
                         std::int64_t maxQty,
                         const QColor& baseColor,
                         qreal /*minLevelRatio — removed: always show every level*/,
                         bool /*towardLowerPrices — band direction derived from level iteration order*/) {
    if (xRight <= xLeft || maxQty <= 0) return;

    // Pixel-honest rendering in two passes:
    //   1. Dim "fill" rect between each pair of adjacent levels. Alpha is
    //      proportional to the near-level's qty/maxQty (dim, ≤ 90).
    //   2. Crisp 1-px bright line at each level price. Alpha scales with
    //      qty/maxQty (floor 40, ceiling 255).
    // Result: visible density gradient plus sharp per-level edges. No
    // band merging, no ratio cutoff — every level inside the viewport
    // is drawn.
    const int xStart = static_cast<int>(std::floor(xLeft));
    const int xEnd = std::max(xStart + 1, static_cast<int>(std::ceil(xRight)));
    const int width = xEnd - xStart;
    const int heightPx = static_cast<int>(std::ceil(vp.h));

    // Pass 1 — dim fill bands between consecutive visible levels.
    painter->setPen(Qt::NoPen);
    bool havePrev = false;
    std::int64_t prevPrice = 0;
    std::int64_t prevQty = 0;
    for (const auto& [price, qty] : levels) {
        if (qty <= 0) continue;
        if (price < vp.pMin || price > vp.pMax) {
            havePrev = false;
            continue;
        }
        if (havePrev) {
            const int yNear = std::clamp(static_cast<int>(std::round(vp.toY(prevPrice))), 0, heightPx);
            const int yFar  = std::clamp(static_cast<int>(std::round(vp.toY(price))),     0, heightPx);
            const int yTop = std::min(yNear, yFar);
            const int yBot = std::max(yNear, yFar);
            if (yBot > yTop) {
                const qreal ratio = std::clamp(
                    static_cast<qreal>(prevQty) / static_cast<qreal>(maxQty), 0.0, 1.0);
                QColor fill = baseColor;
                fill.setAlpha(std::clamp(static_cast<int>(ratio * 110.0), 8, 110));
                painter->setBrush(fill);
                painter->drawRect(xStart, yTop, width, yBot - yTop);
            }
        }
        prevPrice = price;
        prevQty = qty;
        havePrev = true;
    }

    // Pass 2 — crisp 1-px lines on each level.
    painter->setBrush(Qt::NoBrush);
    for (const auto& [price, qty] : levels) {
        if (qty <= 0) continue;
        if (price < vp.pMin || price > vp.pMax) continue;

        const int y = std::clamp(static_cast<int>(std::round(vp.toY(price))), 0, heightPx - 1);
        const qreal ratio = std::clamp(static_cast<qreal>(qty) / static_cast<qreal>(maxQty), 0.0, 1.0);

        QColor color = baseColor;
        color.setAlpha(std::clamp(static_cast<int>(ratio * 255.0), 40, 255));

        QPen linePen(color);
        linePen.setWidth(1);
        linePen.setCapStyle(Qt::SquareCap);
        painter->setPen(linePen);
        painter->drawLine(xStart, y, xEnd - 1, y);
    }
}

}  // namespace

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAntialiasing(false);
    // Render into a GPU FBO when Qt has an OpenGL scene graph. Under the
    // software backend this setting is ignored and we fall back to Image —
    // safe either way.
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
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

void ChartItem::setOrderbookVisible(bool value) {
    if (orderbookVisible_ == value) return;
    orderbookVisible_ = value;
    updateHover_();
    emit orderbookVisibleChanged();
    update();
}

void ChartItem::setBookTickerVisible(bool value) {
    if (bookTickerVisible_ == value) return;
    bookTickerVisible_ = value;
    updateHover_();
    emit bookTickerVisibleChanged();
    update();
}

void ChartItem::setTradeAmountScale(qreal value) {
    value = clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    emit tradeAmountScaleChanged();
    update();
}

void ChartItem::setBookOpacityGain(qreal value) {
    value = clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(bookOpacityGain_ + 1.0, value + 1.0)) return;
    bookOpacityGain_ = value;
    emit bookOpacityGainChanged();
    update();
}

void ChartItem::setBookRenderDetail(qreal value) {
    value = clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(bookRenderDetail_ + 1.0, value + 1.0)) return;
    bookRenderDetail_ = value;
    emit bookRenderDetailChanged();
    update();
}

void ChartItem::setInteractiveMode(bool value) {
    if (interactiveMode_ == value) return;
    interactiveMode_ = value;
    emit interactiveModeChanged();
    update();
}

void ChartItem::setHoverPoint(qreal x, qreal y) {
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = false;
    updateHover_();
    update();
}

void ChartItem::activateContextPoint(qreal x, qreal y) {
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = true;
    updateHover_();
    update();
}

void ChartItem::clearHover() {
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    update();
}

void ChartItem::requestRepaint() {
    updateHover_();
    update();
}

void ChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    if (!hoverActive_ || !controller_ || !controller_->loaded() || width() <= 0 || height() <= 0) return;

    ViewportMap vp{
        controller_->tsMin(),
        controller_->tsMax(),
        controller_->priceMinE8(),
        controller_->priceMaxE8(),
        width(),
        height(),
    };
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin) return;

    if (orderbookVisible_ || bookTickerVisible_) {
        controller_->syncReplayCursorToViewport();
        if (const auto* ticker = controller_->currentBookTicker(); ticker != nullptr) {
            constexpr double lineHitPx = 6.0;
            const double bidDistance = std::abs(vp.toY(ticker->bidPriceE8) - hoverPoint_.y());
            const double askDistance = std::abs(vp.toY(ticker->askPriceE8) - hoverPoint_.y());
            if (bidDistance <= lineHitPx || askDistance <= lineHitPx) {
                hoveredBookKind_ = (bidDistance <= askDistance) ? 1 : 2;
                hoveredBookPriceE8_ = hoveredBookKind_ == 1 ? ticker->bidPriceE8 : ticker->askPriceE8;
                hoveredBookQtyE8_ = hoveredBookKind_ == 1 ? ticker->bidQtyE8 : ticker->askQtyE8;
                hoveredBookTsNs_ = ticker->tsNs;
            } else if (orderbookVisible_) {
                const auto& book = controller_->replay().book();
                std::int64_t priceE8 = 0;
                std::int64_t qtyE8 = 0;
                if (findNearestBookLevel(book.bids(), vp, hoverPoint_.y(), 8.0, priceE8, qtyE8)) {
                    hoveredBookKind_ = 3;
                    hoveredBookPriceE8_ = priceE8;
                    hoveredBookQtyE8_ = qtyE8;
                    hoveredBookTsNs_ = controller_->viewportCursorTs();
                }
                if (findNearestBookLevel(book.asks(), vp, hoverPoint_.y(), 8.0, priceE8, qtyE8)) {
                    const double askDistancePx = std::abs(vp.toY(priceE8) - hoverPoint_.y());
                    const double currentDistancePx = hoveredBookKind_ == 3
                        ? std::abs(vp.toY(hoveredBookPriceE8_) - hoverPoint_.y())
                        : std::numeric_limits<double>::max();
                    if (askDistancePx <= currentDistancePx) {
                        hoveredBookKind_ = 4;
                        hoveredBookPriceE8_ = priceE8;
                        hoveredBookQtyE8_ = qtyE8;
                        hoveredBookTsNs_ = controller_->viewportCursorTs();
                    }
                }
            }
        }
    }

    if (!tradesVisible_) return;

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
    // Pixel-honest mode: no antialiasing, no smoothing, no interpolation.
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);  // text-only AA still OK

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

    auto& replay = controller_->replay();
    controller_->syncReplayCursorToViewport();
    const auto* latestTicker = controller_->currentBookTicker();
    if (orderbookVisible_ || bookTickerVisible_) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        replay.seek(vp.tMin);
        const auto& events = replay.events();
        const auto& tickers = replay.bookTickers();
        // O(log N) lookup — tickers are tsNs-sorted.
        int activeTickerIndex = -1;
        {
            const auto it = std::upper_bound(
                tickers.begin(), tickers.end(), vp.tMin,
                [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept {
                    return ts < row.tsNs;
                });
            if (it != tickers.begin()) {
                activeTickerIndex = static_cast<int>(std::distance(tickers.begin(), it) - 1);
            }
        }

        QPainterPath bidTickerPath;
        QPainterPath askTickerPath;
        bool bidTickerStarted = false;
        bool askTickerStarted = false;

        auto drawHistorySegment = [&](qint64 tsLeft, qint64 tsRight) {
            if (tsRight <= tsLeft) return;
            const qreal xLeft = std::clamp(vp.toX(tsLeft), 0.0, vp.w);
            const qreal xRight = std::clamp(vp.toX(tsRight), 0.0, vp.w);
            if (xRight <= xLeft) return;
            // No pixel-width threshold — every event boundary is honored
            // so nothing visually "disappears" at aggressive zoom levels.

            const auto& book = replay.book();
            if (bookTickerVisible_ && activeTickerIndex >= 0) {
                const auto& ticker = tickers[static_cast<std::size_t>(activeTickerIndex)];
                if (ticker.bidPriceE8 != 0) {
                    appendStepSegment(bidTickerPath, bidTickerStarted, xLeft, xRight, vp.toY(ticker.bidPriceE8));
                }
                if (ticker.askPriceE8 != 0) {
                    appendStepSegment(askTickerPath, askTickerStarted, xLeft, xRight, vp.toY(ticker.askPriceE8));
                }
            }

            if (orderbookVisible_) {
                const auto maxBidQty = std::max<std::int64_t>(maxVisibleQty(book.bids(), vp.pMin, vp.pMax), 1);
                const auto maxAskQty = std::max<std::int64_t>(maxVisibleQty(book.asks(), vp.pMin, vp.pMax), 1);
                // Orderbook uses the book-dedicated palette (green / pink-red),
                // distinct from trade dots.
                drawBookSideSegment(painter, book.bids(), vp, xLeft, xRight, maxBidQty,
                                    bidColor(), 0.0, true);
                drawBookSideSegment(painter, book.asks(), vp, xLeft, xRight, maxAskQty,
                                    askColor(), 0.0, false);
            }
        };

        qint64 segmentStartTs = vp.tMin;
        std::size_t eventCursor = replay.cursor();
        while (eventCursor < events.size() && events[eventCursor].tsNs <= vp.tMax) {
            const auto stampTs = events[eventCursor].tsNs;
            drawHistorySegment(segmentStartTs, stampTs);

            while (eventCursor < events.size() && events[eventCursor].tsNs == stampTs) {
                const auto& ev = events[eventCursor];
                if (ev.kind == hftrec::replay::SessionReplay::EventKind::BookTicker) {
                    activeTickerIndex = static_cast<int>(ev.rowIndex);
                }
                ++eventCursor;
            }

            replay.seek(stampTs);
            segmentStartTs = stampTs;
            eventCursor = replay.cursor();
        }
        drawHistorySegment(segmentStartTs, vp.tMax);

        if (bookTickerVisible_) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            // BookTicker step is part of the orderbook — uses the same palette
            // as book bands. Opaque width=1 lines, miter/square joins, no
            // path-level smoothing; alpha fixed at 255.
            QColor bidStepColor = bidColor();
            bidStepColor.setAlpha(255);
            QPen bidTickerPen(bidStepColor);
            bidTickerPen.setWidth(1);
            bidTickerPen.setJoinStyle(Qt::MiterJoin);
            bidTickerPen.setCapStyle(Qt::SquareCap);
            painter->setPen(bidTickerPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(bidTickerPath);

            QColor askStepColor = askColor();
            askStepColor.setAlpha(255);
            QPen askTickerPen(askStepColor);
            askTickerPen.setWidth(1);
            askTickerPen.setJoinStyle(Qt::MiterJoin);
            askTickerPen.setCapStyle(Qt::SquareCap);
            painter->setPen(askTickerPen);
            painter->drawPath(askTickerPath);

            painter->restore();
        }

        painter->restore();
    }

    if (tradesVisible_) {
        QPen linePen(QColor(150, 150, 155, 160));
        linePen.setWidth(1);
        linePen.setCapStyle(Qt::SquareCap);
        painter->setPen(linePen);

        // Connector polyline only between *adjacent* visible trades — if a
        // trade is filtered (outside viewport in time or price), we break the
        // line so the user never sees a ghost connector across filtered gaps.
        const auto& trades = replay.trades();
        int lastVisibleIdx = -2;
        QPointF previousPoint;
        for (int i = 0; i < static_cast<int>(trades.size()); ++i) {
            const auto& trade = trades[static_cast<std::size_t>(i)];
            if (trade.tsNs < vp.tMin || trade.tsNs > vp.tMax) { lastVisibleIdx = -2; continue; }
            if (trade.priceE8 < vp.pMin || trade.priceE8 > vp.pMax) { lastVisibleIdx = -2; continue; }

            const int x = static_cast<int>(std::round(vp.toX(trade.tsNs)));
            const int y = static_cast<int>(std::round(vp.toY(trade.priceE8)));
            const QPointF point{static_cast<qreal>(x), static_cast<qreal>(y)};

            if (lastVisibleIdx == i - 1) {
                painter->drawLine(previousPoint, point);
            }
            previousPoint = point;
            lastVisibleIdx = i;

            // Trade "dot": integer-snapped square. Size reflects amount in
            // log scale but always integer pixel width — no AA blur.
            const auto amountE8 = multiplyScaledE8(trade.qtyE8, trade.priceE8);
            const qreal radius = amountRadiusScale(amountE8, tradeAmountScale_, interactiveMode_);
            const int side = std::max(1, static_cast<int>(std::round(radius * 2.0)));
            QColor fill = trade.sideBuy ? tradeBuyColor() : tradeSellColor();
            fill.setAlpha(255);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRect(x - side / 2, y - side / 2, side, side);
            painter->setPen(linePen);
        }
    }

    if (contextActive_ && (bookTickerVisible_ || orderbookVisible_) && hoveredTradeIndex_ < 0 &&
        hoveredBookKind_ != 0) {
        const bool isBid = hoveredBookKind_ == 1 || hoveredBookKind_ == 3;
        const auto priceE8 = hoveredBookPriceE8_;
        const auto qtyE8 = hoveredBookQtyE8_;
        const QPointF center{vp.w * 0.5, vp.toY(priceE8)};
        const QColor accent = isBid ? tradeBuyColor() : tradeSellColor();

        QPen focusPen(accent);
        focusPen.setWidthF(1.6);
        painter->setPen(focusPen);
        painter->drawLine(QPointF{0.0, center.y()}, QPointF{vp.w, center.y()});

        QStringList lines;
        lines << QStringLiteral("%1 %2").arg(isBid ? QStringLiteral("BID") : QStringLiteral("ASK"))
                                         .arg(hoveredBookKind_ <= 2 ? QStringLiteral("ticker") : QStringLiteral("book"));
        lines << QStringLiteral("Price  %1").arg(formatScaledE8(priceE8));
        lines << QStringLiteral("Qty    %1").arg(formatTrimmedE8(qtyE8));
        lines << QStringLiteral("Time   %1").arg(formatTimeNs(hoveredBookTsNs_));

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
        const qreal cardX = std::clamp(center.x() + 14.0, 8.0, vp.w - cardWidth - 8.0);
        const qreal cardY = std::clamp(center.y() - cardHeight - 14.0, 8.0, vp.h - cardHeight - 8.0);

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

    if (contextActive_ && tradesVisible_ && hoveredTradeIndex_ >= 0 && hoveredTradeIndex_ < static_cast<int>(replay.trades().size())) {
        const auto& trade = replay.trades()[static_cast<std::size_t>(hoveredTradeIndex_)];
        if (trade.tsNs >= vp.tMin && trade.tsNs <= vp.tMax && trade.priceE8 >= vp.pMin && trade.priceE8 <= vp.pMax) {
            const QPointF center{vp.toX(trade.tsNs), vp.toY(trade.priceE8)};
            const QColor accent = trade.sideBuy ? tradeBuyColor() : tradeSellColor();
            const auto amountE8 = multiplyScaledE8(trade.qtyE8, trade.priceE8);
            const qreal tradeRadius = amountRadiusScale(amountE8, tradeAmountScale_, false);

            QPen haloPen(trade.sideBuy ? haloBuyColor() : haloSellColor());
            haloPen.setWidthF(3.0);
            painter->setPen(haloPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, tradeRadius + 3.5, tradeRadius + 3.5);

            QPen focusPen(accent);
            focusPen.setWidthF(1.3);
            painter->setPen(focusPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, tradeRadius + 1.7, tradeRadius + 1.7);

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
