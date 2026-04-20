#include "gui/viewer/ChartController.hpp"

#include <QDateTime>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <limits>

#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer {

namespace {

constexpr std::int64_t kOneMsNs = 1000000ll;
constexpr std::size_t kViewportBookLevelsPerSide = 24;
constexpr std::size_t kRenderBookLevelsBudgetPerSide = 256;
constexpr std::size_t kInteractiveBookLevelsBudgetPerSide = 192;
constexpr std::int64_t kUsdScaleE8 = 100000000ll;
constexpr double kTradeDenseMultiplierInteractive = 2.25;
constexpr double kTradeDenseMultiplierStatic = 5.0;

std::int64_t ceilToStep(std::int64_t value, std::int64_t step) {
    if (step <= 0) return value;
    if (value >= 0) return ((value + step - 1) / step) * step;
    return (value / step) * step;
}

std::int64_t floorToStep(std::int64_t value, std::int64_t step) {
    if (step <= 0) return value;
    if (value >= 0) return (value / step) * step;
    return ((value - step + 1) / step) * step;
}

std::int64_t nicePriceStepE8(std::int64_t spanE8, int tickCount) {
    const auto safeTicks = std::max(2, tickCount);
    std::int64_t rawStep = spanE8 / static_cast<std::int64_t>(safeTicks - 1);
    rawStep = std::max<std::int64_t>(rawStep, 1);

    std::int64_t magnitude = 1;
    while (magnitude <= rawStep / 10 && magnitude <= (std::numeric_limits<std::int64_t>::max() / 10)) {
        magnitude *= 10;
    }

    for (;;) {
        const std::int64_t candidates[] = {magnitude, magnitude * 2, magnitude * 5, magnitude * 10};
        for (const auto candidate : candidates) {
            if (candidate >= rawStep) return candidate;
        }
        magnitude *= 10;
    }
}

std::int64_t niceTimeStepNs(std::int64_t spanNs, int tickCount) {
    static constexpr std::int64_t kCandidates[] = {
        100ll * kOneMsNs, 200ll * kOneMsNs, 500ll * kOneMsNs, 1000ll * kOneMsNs,
        2000ll * kOneMsNs, 5000ll * kOneMsNs, 10000ll * kOneMsNs, 15000ll * kOneMsNs,
        30000ll * kOneMsNs, 60000ll * kOneMsNs, 120000ll * kOneMsNs, 300000ll * kOneMsNs,
    };

    const auto safeTicks = std::max(2, tickCount);
    const std::int64_t rawStep =
        std::max<std::int64_t>(spanNs / static_cast<std::int64_t>(safeTicks - 1), 100ll * kOneMsNs);
    for (const auto candidate : kCandidates) {
        if (candidate >= rawStep) return candidate;
    }
    return kCandidates[std::size(kCandidates) - 1];
}

std::int64_t usdToE8(qreal usd) noexcept {
    const qreal clamped = std::clamp<qreal>(usd, 100.0, 100000.0);
    return static_cast<std::int64_t>(std::llround(clamped * static_cast<qreal>(kUsdScaleE8)));
}

void absorbPrice(std::int64_t price, bool& hasPrice, std::int64_t& pMin, std::int64_t& pMax) {
    if (price <= 0) return;
    if (!hasPrice) {
        pMin = price;
        pMax = price;
        hasPrice = true;
        return;
    }
    pMin = std::min(pMin, price);
    pMax = std::max(pMax, price);
}

template <typename MapT>
void absorbBookLevels(const MapT& levels,
                      std::size_t maxLevels,
                      bool& hasPrice,
                      std::int64_t& pMin,
                      std::int64_t& pMax) {
    std::size_t seen = 0;
    for (const auto& [price, qty] : levels) {
        if (seen >= maxLevels) break;
        if (qty <= 0) continue;
        absorbPrice(price, hasPrice, pMin, pMax);
        ++seen;
    }
}

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

QString formatShortTimeNs(std::int64_t tsNs) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);
    return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
}

QVariantList buildAxisTicks(int tickCount, const auto& formatter) {
    QVariantList ticks;
    const int safeTicks = std::max(2, tickCount);
    ticks.reserve(safeTicks);
    for (int i = 0; i < safeTicks; ++i) {
        const double ratio = static_cast<double>(i) / static_cast<double>(std::max(1, safeTicks - 1));
        QVariantMap tick;
        tick.insert(QStringLiteral("ratio"), ratio);
        tick.insert(QStringLiteral("text"), formatter(ratio));
        ticks.push_back(tick);
    }
    return ticks;
}

struct TradePixelAccumulator {
    bool hasLow{false};
    bool hasHigh{false};
    TradeDot low{};
    TradeDot high{};

    void absorb(const TradeDot& dot) noexcept {
        if (!hasLow || dot.priceE8 < low.priceE8) {
            low = dot;
            hasLow = true;
        }
        if (!hasHigh || dot.priceE8 > high.priceE8) {
            high = dot;
            hasHigh = true;
        }
    }

    void appendTo(std::vector<TradeDot>& out) const {
        if (!hasLow) return;
        if (hasLow && hasHigh && low.origIndex > high.origIndex) {
            out.push_back(high);
            if (low.origIndex != high.origIndex) out.push_back(low);
            return;
        }
        out.push_back(low);
        if (hasHigh && high.origIndex != low.origIndex) out.push_back(high);
    }
};

struct TradePixelBin {
    int xPx{-1};
    TradePixelAccumulator buy{};
    TradePixelAccumulator sell{};

    void reset(int nextXPx) noexcept {
        xPx = nextXPx;
        buy = TradePixelAccumulator{};
        sell = TradePixelAccumulator{};
    }

    void appendTo(std::vector<TradeDot>& out) const {
        buy.appendTo(out);
        sell.appendTo(out);
    }
};

bool shouldDecimateTrades(std::size_t visibleTradeCount,
                          qreal widthPx,
                          bool interactiveMode) noexcept {
    if (visibleTradeCount <= 1u || widthPx <= 0.0) return false;
    const double multiplier = interactiveMode
        ? kTradeDenseMultiplierInteractive
        : kTradeDenseMultiplierStatic;
    const double budget = std::max(32.0, static_cast<double>(widthPx) * multiplier);
    return static_cast<double>(visibleTradeCount) > budget;
}

}  // namespace

void ChartController::computeInitialViewport_() {
    tsMin_ = replay_.firstTsNs();
    tsMax_ = replay_.lastTsNs();
    if (tsMax_ == tsMin_) tsMax_ = tsMin_ + 1;

    std::int64_t pMin = 0;
    std::int64_t pMax = 0;
    bool hasPrice = false;

    for (const auto& trade : replay_.trades()) {
        absorbPrice(trade.priceE8, hasPrice, pMin, pMax);
    }
    for (const auto& ticker : replay_.bookTickers()) {
        absorbPrice(ticker.bidPriceE8, hasPrice, pMin, pMax);
        absorbPrice(ticker.askPriceE8, hasPrice, pMin, pMax);
    }

    absorbBookLevels(replay_.book().bids(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);
    absorbBookLevels(replay_.book().asks(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);

    if (!hasPrice) {
        pMin = 0;
        pMax = 1;
    }

    if (pMax <= pMin) pMax = pMin + 1;
    const std::int64_t pad = (pMax - pMin) / 10 + 1;
    priceMinE8_ = std::max<std::int64_t>(0, pMin - pad);
    priceMaxE8_ = pMax + pad;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
}

void ChartController::setViewport(qint64 tsMin, qint64 tsMax, qint64 priceMinE8, qint64 priceMaxE8) {
    if (tsMax <= tsMin || priceMaxE8 <= priceMinE8) return;
    tsMin_ = tsMin;
    tsMax_ = tsMax;
    priceMinE8_ = priceMinE8;
    priceMaxE8_ = priceMaxE8;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panTime(double fraction) {
    const qint64 w = tsMax_ - tsMin_;
    const qint64 dt = static_cast<qint64>(static_cast<double>(w) * fraction);
    tsMin_ += dt;
    tsMax_ += dt;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panPrice(double fraction) {
    const qint64 h = priceMaxE8_ - priceMinE8_;
    const qint64 dp = static_cast<qint64>(static_cast<double>(h) * fraction);
    priceMinE8_ += dp;
    priceMaxE8_ += dp;
    emit viewportChanged();
}

void ChartController::zoomTime(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (tsMin_ + tsMax_) / 2;
    const qint64 halfW = static_cast<qint64>(static_cast<double>(tsMax_ - tsMin_) / (2.0 * factor));
    tsMin_ = centre - halfW;
    tsMax_ = centre + halfW;
    if (tsMax_ <= tsMin_) tsMax_ = tsMin_ + 1;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::zoomPrice(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (priceMinE8_ + priceMaxE8_) / 2;
    const qint64 halfH = static_cast<qint64>(static_cast<double>(priceMaxE8_ - priceMinE8_) / (2.0 * factor));
    priceMinE8_ = centre - halfH;
    priceMaxE8_ = centre + halfH;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
    emit viewportChanged();
}

void ChartController::autoFit() {
    if (!loaded_) return;
    computeInitialViewport_();
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToStart() {
    const qint64 w = tsMax_ - tsMin_;
    tsMin_ = replay_.firstTsNs();
    tsMax_ = tsMin_ + w;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToEnd() {
    const qint64 w = tsMax_ - tsMin_;
    tsMax_ = replay_.lastTsNs();
    tsMin_ = tsMax_ - w;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

QString ChartController::formatPriceAt(double ratio) const {
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    const auto span = priceMaxE8_ - priceMinE8_;
    const auto value = priceMaxE8_ - static_cast<qint64>(static_cast<double>(span) * ratio);
    return formatScaledE8(value);
}

QString ChartController::formatTimeAt(double ratio) const {
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    const auto span = tsMax_ - tsMin_;
    const auto value = tsMin_ + static_cast<qint64>(static_cast<double>(span) * ratio);
    return formatShortTimeNs(value);
}

QVariantList ChartController::priceScaleTicks(int tickCount) const {
    return buildAxisTicks(tickCount, [this](double ratio) {
        return formatPriceAt(ratio);
    });
}

QVariantList ChartController::timeScaleTicks(int tickCount) const {
    return buildAxisTicks(tickCount, [this](double ratio) {
        return formatTimeAt(ratio);
    });
}

QString ChartController::formatPriceScaleLabel(int index, int tickCount) const {
    const auto safeTicks = std::max(2, tickCount);
    const auto step = nicePriceStepE8(std::max<std::int64_t>(priceMaxE8_ - priceMinE8_, 1), safeTicks);
    const auto top = ceilToStep(priceMaxE8_, step);
    const auto value = top - static_cast<std::int64_t>(index) * step;
    return formatScaledE8(value);
}

QString ChartController::formatTimeScaleLabel(int index, int tickCount) const {
    const auto safeTicks = std::max(2, tickCount);
    const auto step = niceTimeStepNs(std::max<std::int64_t>(tsMax_ - tsMin_, 1), safeTicks);
    const auto start = floorToStep(tsMin_, step);
    const auto value = start + static_cast<std::int64_t>(index) * step;
    return formatShortTimeNs(value);
}

std::int64_t ChartController::viewportCursorTs() const noexcept {
    return (tsMin_ + tsMax_) / 2;
}

void ChartController::syncReplayCursorToViewport() {
    if (!loaded_) {
        currentBookTickerIndex_ = -1;
        return;
    }

    const auto cursorTs = viewportCursorTs();
    replay_.seek(cursorTs);
    currentBookTickerIndex_ = -1;

    const auto& tickers = replay_.bookTickers();
    for (std::size_t i = 0; i < tickers.size(); ++i) {
        if (tickers[i].tsNs > cursorTs) break;
        currentBookTickerIndex_ = static_cast<std::int64_t>(i);
    }
}

const hftrec::replay::BookTickerRow* ChartController::currentBookTicker() const noexcept {
    if (currentBookTickerIndex_ < 0) return nullptr;
    const auto index = static_cast<std::size_t>(currentBookTickerIndex_);
    if (index >= replay_.bookTickers().size()) return nullptr;
    return &replay_.bookTickers()[index];
}

RenderSnapshot ChartController::buildSnapshot(qreal widthPx, qreal heightPx, const SnapshotInputs& in) {
    RenderSnapshot snap{};
    snap.vp = ViewportMap{tsMin_, tsMax_, priceMinE8_, priceMaxE8_,
                          static_cast<double>(widthPx), static_cast<double>(heightPx)};
    snap.loaded = loaded_;
    snap.tradesVisible = in.tradesVisible;
    snap.orderbookVisible = in.orderbookVisible;
    snap.bookTickerVisible = in.bookTickerVisible;
    snap.interactiveMode = in.interactiveMode;
    snap.overlayOnly = in.overlayOnly;
    snap.exactTradeRendering = in.exactTradeRendering;
    snap.tradeAmountScale = in.tradeAmountScale;
    snap.bookOpacityGain = in.bookOpacityGain;
    snap.bookRenderDetail = in.bookRenderDetail;

    if (!loaded_ || widthPx <= 0.0 || heightPx <= 0.0) return snap;
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return snap;

    const auto minVisibleAmountE8 = usdToE8(in.bookRenderDetail);
    const auto heightLimit = static_cast<double>(heightPx);

    const auto& trades = replay_.trades();
    std::size_t visibleTradeCount = 0;
    for (const auto& trade : trades) {
        if (trade.tsNs < snap.vp.tMin || trade.tsNs > snap.vp.tMax) continue;
        if (trade.priceE8 < snap.vp.pMin || trade.priceE8 > snap.vp.pMax) continue;
        ++visibleTradeCount;
    }

    snap.tradeDecimated = !in.exactTradeRendering
        && shouldDecimateTrades(visibleTradeCount, widthPx, in.interactiveMode);
    snap.tradeDots.reserve(
        snap.tradeDecimated
            ? static_cast<std::size_t>(std::max<qreal>(32.0, std::ceil(widthPx) * 4.0))
            : visibleTradeCount);

    TradePixelBin pixelBin{};
    bool pixelBinActive = false;
    auto flushTradeBin = [&]() {
        if (!pixelBinActive) return;
        pixelBin.appendTo(snap.tradeDots);
        pixelBinActive = false;
    };

    for (int i = 0; i < static_cast<int>(trades.size()); ++i) {
        const auto& t = trades[static_cast<std::size_t>(i)];
        if (t.tsNs < snap.vp.tMin || t.tsNs > snap.vp.tMax) continue;
        if (t.priceE8 < snap.vp.pMin || t.priceE8 > snap.vp.pMax) continue;
        const auto x = snap.vp.toX(t.tsNs);
        const auto y = snap.vp.toY(t.priceE8);
        if (x < 0.0 || x > snap.vp.w || y < 0.0 || y > snap.vp.h) continue;

        const TradeDot dot{t.tsNs, t.priceE8, t.qtyE8, t.sideBuy != 0, i};
        if (!snap.tradeDecimated) {
            snap.tradeDots.push_back(dot);
            continue;
        }

        const int xPx = std::clamp(
            static_cast<int>(std::floor(x)),
            0,
            std::max(0, static_cast<int>(std::ceil(widthPx)) - 1));
        if (!pixelBinActive || pixelBin.xPx != xPx) {
            flushTradeBin();
            pixelBin.reset(xPx);
            pixelBinActive = true;
        }

        if (dot.sideBuy) pixelBin.buy.absorb(dot);
        else pixelBin.sell.absorb(dot);
    }
    flushTradeBin();

    if (!in.orderbookVisible && !in.bookTickerVisible) return snap;

    const std::int64_t coverageStart = std::max<std::int64_t>(snap.vp.tMin, replay_.firstTsNs());
    const std::int64_t coverageEnd = std::min<std::int64_t>(snap.vp.tMax, replay_.lastTsNs());
    if (coverageEnd <= coverageStart) return snap;

    replay_.seek(coverageStart);
    const auto& buckets = replay_.buckets();
    const auto& tickers = replay_.bookTickers();

    int activeTickerIndex = -1;
    const auto it = std::upper_bound(
        tickers.begin(), tickers.end(), coverageStart,
        [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept { return ts < row.tsNs; });
    if (it != tickers.begin()) {
        activeTickerIndex = static_cast<int>(std::distance(tickers.begin(), it) - 1);
    }

    auto emitSegment = [&](std::int64_t tsStart, std::int64_t tsEnd) {
        if (tsEnd <= tsStart) return;
        const qreal xLeft = std::clamp(snap.vp.toX(tsStart), 0.0, snap.vp.w);
        const qreal xRight = std::clamp(snap.vp.toX(tsEnd), 0.0, snap.vp.w);
        const int xStartPx = static_cast<int>(std::floor(xLeft));
        const int xEndPx = static_cast<int>(std::ceil(xRight));
        if ((xEndPx - xStartPx) < 1) return;

        BookSegment seg;
        seg.tsStartNs = tsStart;
        seg.tsEndNs = tsEnd;
        const auto& book = replay_.book();
        if (in.orderbookVisible) {
            std::int64_t maxBid = 0;
            std::int64_t maxAsk = 0;
            std::size_t keptBids = 0;
            std::size_t keptAsks = 0;
            const std::size_t levelsBudget = in.interactiveMode
                ? kInteractiveBookLevelsBudgetPerSide
                : kRenderBookLevelsBudgetPerSide;
            for (const auto& [price, qty] : book.bids()) {
                if (price < snap.vp.pMin || price > snap.vp.pMax || qty <= 0) continue;
                if (keptBids >= levelsBudget) break;
                const auto y = snap.vp.toY(price);
                if (y < 0.0 || y >= heightLimit) continue;
                const auto amountE8 = detail::multiplyScaledE8(qty, price);
                if (amountE8 < minVisibleAmountE8) continue;
                seg.bids.push_back(BookLevel{price, qty});
                if (qty > maxBid) maxBid = qty;
                ++keptBids;
            }
            for (const auto& [price, qty] : book.asks()) {
                if (price < snap.vp.pMin || price > snap.vp.pMax || qty <= 0) continue;
                if (keptAsks >= levelsBudget) break;
                const auto y = snap.vp.toY(price);
                if (y < 0.0 || y >= heightLimit) continue;
                const auto amountE8 = detail::multiplyScaledE8(qty, price);
                if (amountE8 < minVisibleAmountE8) continue;
                seg.asks.push_back(BookLevel{price, qty});
                if (qty > maxAsk) maxAsk = qty;
                ++keptAsks;
            }
            seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
            seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
        }
        bool hasVisibleTicker = false;
        if (in.bookTickerVisible && activeTickerIndex >= 0) {
            const auto& tk = tickers[static_cast<std::size_t>(activeTickerIndex)];
            const auto bidY = snap.vp.toY(tk.bidPriceE8);
            const auto askY = snap.vp.toY(tk.askPriceE8);
            if (tk.bidPriceE8 > 0 && bidY >= 0.0 && bidY < heightLimit) {
                seg.tickerBidE8 = tk.bidPriceE8;
                seg.tickerBidQtyE8 = tk.bidQtyE8;
                hasVisibleTicker = true;
            }
            if (tk.askPriceE8 > 0 && askY >= 0.0 && askY < heightLimit) {
                seg.tickerAskE8 = tk.askPriceE8;
                seg.tickerAskQtyE8 = tk.askQtyE8;
                hasVisibleTicker = true;
            }
        }
        if (seg.bids.empty() && seg.asks.empty() && !hasVisibleTicker) return;
        snap.bookSegments.push_back(std::move(seg));
    };

    std::int64_t segStart = coverageStart;
    std::size_t bucketCursor = replay_.cursor();
    while (bucketCursor < buckets.size() && buckets[bucketCursor].tsNs <= coverageEnd) {
        const auto stampTs = buckets[bucketCursor].tsNs;
        const double xStart = snap.vp.toX(segStart);
        const double xStamp = snap.vp.toX(stampTs);
        const bool wide = (xStamp - xStart) >= 1.0;

        if (wide) {
            emitSegment(segStart, stampTs);
            segStart = stampTs;
        }
        for (const auto& item : buckets[bucketCursor].items) {
            if (item.kind == hftrec::replay::SessionReplay::EventKind::BookTicker) {
                activeTickerIndex = static_cast<int>(item.rowIndex);
            }
        }
        replay_.seek(stampTs);
        bucketCursor = replay_.cursor();
    }
    emitSegment(segStart, coverageEnd);

    return snap;
}

}  // namespace hftrec::gui::viewer
