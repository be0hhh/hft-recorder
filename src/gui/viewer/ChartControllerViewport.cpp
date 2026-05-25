#include "gui/viewer/ChartController.hpp"

#include <QDateTime>
#include <QColor>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/detail/BookTickerTraceBuild.hpp"
#include "gui/viewer/detail/BookMath.hpp"
#include "gui/viewer/detail/Formatters.hpp"
#include "gui/viewer/detail/TradeGrouping.hpp"

namespace hftrec::gui::viewer {

namespace {

constexpr std::int64_t kOneMsNs = 1000000ll;
constexpr std::size_t kViewportBookLevelsPerSide = 24;
constexpr std::size_t kRenderBookLevelsBudgetPerSide = 96;
constexpr std::size_t kInteractiveBookLevelsBudgetPerSide = 48;
constexpr std::int64_t kUsdScaleE8 = 100000000ll;
constexpr double kTradeLodEnterMultiplier = 4.0;
constexpr double kTradeLodExitMultiplier = 3.0;
constexpr std::size_t kTradeExactExitBudget = 20000;
constexpr std::size_t kTradeAggregateEnterBudget = 24000;
constexpr double kBookMinSegmentWidthPxStatic = 2.0;
constexpr double kBookMinSegmentWidthPxInteractive = 4.0;

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

void appendGpuVerticesForSide(std::vector<GpuBookLineVertex>& out,
                              const std::vector<BookLevel>& levels,
                              const ViewportMap& vp,
                              qreal xLeft,
                              qreal xRight,
                              const QColor& baseColor,
                              std::int64_t brightnessRefE8,
                              std::int64_t minVisibleAmountE8) {
    if (xRight <= xLeft || levels.empty()) return;

    const int xStart = static_cast<int>(std::floor(xLeft));
    const int xEnd = std::max(xStart + 1, static_cast<int>(std::ceil(xRight)));
    if (xEnd <= xStart) return;

    int lastDrawnY = std::numeric_limits<int>::min();
    for (const auto& level : levels) {
        int y = 0;
        std::uint8_t alpha = 0;
        if (!prepareVisibleLevelForScreen(
                level, vp, minVisibleAmountE8, brightnessRefE8, y, alpha)) {
            continue;
        }
        if (y == lastDrawnY) continue;

        out.push_back(GpuBookLineVertex{
            static_cast<float>(xStart),
            static_cast<float>(y),
            static_cast<std::uint8_t>(baseColor.red()),
            static_cast<std::uint8_t>(baseColor.green()),
            static_cast<std::uint8_t>(baseColor.blue()),
            alpha,
        });
        out.push_back(GpuBookLineVertex{
            static_cast<float>(xEnd - 1),
            static_cast<float>(y),
            static_cast<std::uint8_t>(baseColor.red()),
            static_cast<std::uint8_t>(baseColor.green()),
            static_cast<std::uint8_t>(baseColor.blue()),
            alpha,
        });
        lastDrawnY = y;
    }
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

QString formatShortTimeNs(std::int64_t tsNs, std::int64_t spanNs = 1000000ll) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);

    const auto absTsNs = tsNs >= 0 ? tsNs : -tsNs;
    const auto usPart = static_cast<int>((absTsNs / 1000ll) % 1000ll);
    const auto nsPart = static_cast<int>(absTsNs % 1000ll);
    const QString base = dt.toString(QStringLiteral("HH:mm:ss.zzz"));
    if (spanNs < 1000ll) {
        return QStringLiteral("%1.%2.%3")
            .arg(base)
            .arg(usPart, 3, 10, QLatin1Char('0'))
            .arg(nsPart, 3, 10, QLatin1Char('0'));
    }
    if (spanNs < 1000000ll) {
        return QStringLiteral("%1.%2")
            .arg(base)
            .arg(usPart, 3, 10, QLatin1Char('0'));
    }
    return base;
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

struct TradeLodDecision {
    bool aggregate{false};
    std::size_t enterBudget{0};
    std::size_t exitBudget{0};
};

TradeLodDecision decideTradeLod(std::size_t visibleTradeCount,
                                qreal widthPx,
                                bool forceExact,
                                bool previousAggregated) noexcept {
    (void)previousAggregated;
    const std::size_t width = widthPx <= 0.0
        ? 0u
        : static_cast<std::size_t>(std::ceil(widthPx));
    const std::size_t enterBudget = std::max<std::size_t>(
        kTradeAggregateEnterBudget,
        static_cast<std::size_t>(std::ceil(widthPx * kTradeLodEnterMultiplier)));
    const std::size_t exitBudget = std::max<std::size_t>(
        kTradeExactExitBudget,
        static_cast<std::size_t>(std::ceil(widthPx * kTradeLodExitMultiplier)));
    if (forceExact || visibleTradeCount <= 1u || width == 0u) return TradeLodDecision{false, enterBudget, exitBudget};
    return TradeLodDecision{false, enterBudget, exitBudget};
}

struct TradePixelBucket {
    int xPx{-1};
    bool active{false};
    TradeDot dot{};
    std::int64_t largestAmountE8{0};

    void reset(int nextXPx) noexcept {
        xPx = nextXPx;
        active = false;
        dot = TradeDot{};
        largestAmountE8 = 0;
    }

    void absorb(std::int64_t tsNs,
                std::int64_t priceE8,
                std::int64_t qtyE8,
                bool sideBuy,
                int origIndex) noexcept {
        const std::int64_t amountE8 = detail::multiplyScaledE8(qtyE8, priceE8);
        if (!active) {
            active = true;
            dot.tsNs = tsNs;
            dot.tsStartNs = tsNs;
            dot.tsEndNs = tsNs;
            dot.priceE8 = priceE8;
            dot.representativePriceE8 = priceE8;
            dot.qtyE8 = qtyE8;
            dot.sideBuy = sideBuy;
            dot.origIndex = origIndex;
            dot.firstOrigIndex = origIndex;
            dot.lastOrigIndex = origIndex;
            dot.aggregated = true;
            dot.tradeCount = 0;
        }

        dot.tsStartNs = std::min(dot.tsStartNs, tsNs);
        dot.tsEndNs = std::max(dot.tsEndNs, tsNs);
        dot.firstOrigIndex = dot.firstOrigIndex < 0 ? origIndex : std::min(dot.firstOrigIndex, origIndex);
        dot.lastOrigIndex = dot.lastOrigIndex < 0 ? origIndex : std::max(dot.lastOrigIndex, origIndex);
        ++dot.tradeCount;
        dot.totalQtyE8 += qtyE8;
        dot.totalAmountE8 += amountE8;
        if (sideBuy) {
            dot.buyQtyE8 += qtyE8;
            dot.buyAmountE8 += amountE8;
        } else {
            dot.sellQtyE8 += qtyE8;
            dot.sellAmountE8 += amountE8;
        }
        if (amountE8 > largestAmountE8) {
            largestAmountE8 = amountE8;
            dot.tsNs = tsNs;
            dot.qtyE8 = qtyE8;
            dot.sideBuy = sideBuy;
            dot.origIndex = origIndex;
            dot.representativePriceE8 = priceE8;
        }
    }

    void appendTo(std::vector<TradeDot>& out) noexcept {
        if (!active || dot.tradeCount <= 0) return;
        if (dot.totalQtyE8 > 0 && dot.totalAmountE8 > 0) {
            const long double scaledPrice =
                (static_cast<long double>(dot.totalAmountE8) * 100000000.0L)
                / static_cast<long double>(dot.totalQtyE8);
            dot.priceE8 = static_cast<std::int64_t>(std::llround(scaledPrice));
        }
        dot.tsNs = dot.tsStartNs + (dot.tsEndNs - dot.tsStartNs) / 2;
        dot.groupEntries.clear();
        out.push_back(dot);
        active = false;
    }
};

std::size_t bookLevelCandidateBudget(std::size_t levelsBudget) noexcept {
    return std::max<std::size_t>(levelsBudget * 8u, levelsBudget);
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

struct BookTickerAnchor {
    std::int64_t bidPriceE8{0};
    std::int64_t askPriceE8{0};
};

BookTickerAnchor bookTickerAnchorAtOrBefore(const std::vector<hftrec::replay::BookTickerRow>& tickers,
                                            std::int64_t tsNs) noexcept {
    const auto it = std::upper_bound(
        tickers.begin(),
        tickers.end(),
        tsNs,
        [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept {
            return ts < row.tsNs;
        });
    auto cursor = it;
    while (cursor != tickers.begin()) {
        --cursor;
        if (cursor->bidPriceE8 > 0 || cursor->askPriceE8 > 0) {
            return BookTickerAnchor{cursor->bidPriceE8, cursor->askPriceE8};
        }
    }
    return BookTickerAnchor{};
}

void buildBookTickerTrace(RenderSnapshot& snap,
                          const std::vector<hftrec::replay::BookTickerRow>& tickers,
                          std::int64_t renderMinTs,
                          std::int64_t renderMaxTs) {
    snap.bookTickerTrace = BookTickerTrace{};
    if (!snap.bookTickerVisible || tickers.empty() || snap.vp.w <= 0.0 || snap.vp.h <= 0.0) return;
    renderMinTs = std::max<std::int64_t>(renderMinTs, snap.vp.tMin);
    renderMaxTs = std::min<std::int64_t>(renderMaxTs, snap.vp.tMax);
    if (renderMaxTs < renderMinTs) return;

    const auto firstInRange = std::lower_bound(
        tickers.begin(),
        tickers.end(),
        renderMinTs,
        [](const hftrec::replay::BookTickerRow& row, std::int64_t ts) noexcept {
            return row.tsNs < ts;
        });

    std::size_t index = static_cast<std::size_t>(std::distance(tickers.begin(), firstInRange));
    auto& trace = snap.bookTickerTrace;
    trace.samples.reserve(std::max<std::size_t>(static_cast<std::size_t>(snap.vp.w / 4.0) + 2u,
                                                static_cast<std::size_t>(std::distance(firstInRange, tickers.end()))));
    trace.bidLines.reserve((static_cast<std::size_t>(std::distance(firstInRange, tickers.end())) + 1u) * 2u);
    trace.askLines.reserve(trace.bidLines.capacity());

    detail::BookTickerTraceBuildState state{};
    if (firstInRange != tickers.begin()) {
        const auto& carry = *std::prev(firstInRange);
        if (carry.bidPriceE8 > 0 || carry.askPriceE8 > 0) {
            const std::int64_t endTs = index < tickers.size()
                ? std::min<std::int64_t>(tickers[index].tsNs, renderMaxTs)
                : renderMaxTs;
            detail::appendBookTickerTraceSegment(trace, state, snap.vp, carry, renderMinTs, endTs, true, false);
        }
    }

    while (index < tickers.size()) {
        const auto& ticker = tickers[index];
        if (ticker.tsNs > renderMaxTs) break;
        const std::int64_t endTs = (index + 1u < tickers.size())
            ? std::min<std::int64_t>(tickers[index + 1u].tsNs, renderMaxTs)
            : renderMaxTs;
        const bool hasNextAtEnd = index + 1u < tickers.size() && tickers[index + 1u].tsNs <= renderMaxTs;
        detail::appendBookTickerTraceSegment(trace,
                                             state,
                                             snap.vp,
                                             ticker,
                                             std::max<std::int64_t>(ticker.tsNs, renderMinTs),
                                             endTs,
                                             true,
                                             !hasNextAtEnd);
        ++index;
    }
}

void buildLatestBookTickerTrace(RenderSnapshot& snap,
                                const std::vector<hftrec::replay::BookTickerRow>& tickers,
                                std::int64_t latestTsNs) {
    snap.bookTickerTrace = BookTickerTrace{};
    if (!snap.bookTickerVisible || tickers.empty() || snap.vp.w <= 0.0) return;
    const auto it = std::upper_bound(
        tickers.begin(),
        tickers.end(),
        latestTsNs,
        [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept {
            return ts < row.tsNs;
        });
    if (it == tickers.begin()) return;
    const auto& ticker = *std::prev(it);
    if (ticker.tsNs < snap.vp.tMin || ticker.tsNs > snap.vp.tMax) return;

    detail::BookTickerTraceBuildState state{};
    detail::appendBookTickerTraceSegment(snap.bookTickerTrace,
                                         state,
                                         snap.vp,
                                         ticker,
                                         ticker.tsNs,
                                         ticker.tsNs,
                                         true,
                                         true);
}

}  // namespace

void ChartController::computeInitialViewport_() {
    std::int64_t marketTsMin = 0;
    std::int64_t marketTsMax = 0;
    bool hasMarketTs = false;
    const auto absorbTs = [&](const auto& rows) noexcept {
        if (rows.empty()) return;
        const auto first = rows.front().tsNs;
        const auto last = rows.back().tsNs;
        if (!hasMarketTs) {
            marketTsMin = first;
            marketTsMax = last;
            hasMarketTs = true;
            return;
        }
        marketTsMin = std::min(marketTsMin, first);
        marketTsMax = std::max(marketTsMax, last);
    };
    absorbTs(replay_.trades());
    absorbTs(replay_.bookTickers());
    absorbTs(replay_.depths());
    if (!hasMarketTs) absorbTs(replay_.liquidations());
    if (!hasMarketTs) absorbTs(replay_.candles());

    tsMin_ = hasMarketTs ? marketTsMin : replay_.firstTsNs();
    tsMax_ = hasMarketTs ? marketTsMax : replay_.lastTsNs();
    if (tsMax_ == tsMin_) tsMax_ = tsMin_ + 1;

    std::int64_t pMin = 0;
    std::int64_t pMax = 0;
    bool hasPrice = false;
    bool hasTradeOrTickerPrice = false;

    for (const auto& trade : replay_.trades()) {
        absorbPrice(trade.priceE8, hasPrice, pMin, pMax);
        hasTradeOrTickerPrice = hasPrice;
    }
    for (const auto& ticker : replay_.bookTickers()) {
        absorbPrice(ticker.bidPriceE8, hasPrice, pMin, pMax);
        absorbPrice(ticker.askPriceE8, hasPrice, pMin, pMax);
        if (ticker.bidPriceE8 > 0 || ticker.askPriceE8 > 0) hasTradeOrTickerPrice = true;
    }

    if (!hasTradeOrTickerPrice) {
        absorbBookLevels(replay_.book().bids(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);
        absorbBookLevels(replay_.book().asks(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);
    }

    if (!hasPrice) {
        for (const auto& liquidation : replay_.liquidations()) {
            absorbPrice(liquidation.priceE8, hasPrice, pMin, pMax);
        }
    }

    if (!hasPrice) {
        for (const auto& candle : replay_.candles()) {
            absorbPrice(candle.lowE8, hasPrice, pMin, pMax);
            absorbPrice(candle.highE8, hasPrice, pMin, pMax);
        }
    }

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
    markUserViewportControl_();
    tsMin_ = tsMin;
    tsMax_ = tsMax;
    priceMinE8_ = priceMinE8;
    priceMaxE8_ = priceMaxE8;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panTime(double fraction) {
    markUserViewportControl_();
    const qint64 w = tsMax_ - tsMin_;
    const qint64 dt = static_cast<qint64>(static_cast<double>(w) * fraction);
    tsMin_ += dt;
    tsMax_ += dt;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panPrice(double fraction) {
    markUserViewportControl_();
    const qint64 h = priceMaxE8_ - priceMinE8_;
    const qint64 dp = static_cast<qint64>(static_cast<double>(h) * fraction);
    priceMinE8_ += dp;
    priceMaxE8_ += dp;
    emit viewportChanged();
}

void ChartController::zoomTime(double factor) {
    if (factor <= 0.0) return;
    markUserViewportControl_();
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
    markUserViewportControl_();
    const qint64 centre = (priceMinE8_ + priceMaxE8_) / 2;
    const qint64 halfH = static_cast<qint64>(static_cast<double>(priceMaxE8_ - priceMinE8_) / (2.0 * factor));
    priceMinE8_ = centre - halfH;
    priceMaxE8_ = centre + halfH;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
    emit viewportChanged();
}

void ChartController::autoFit() {
    if (!loaded_) return;
    liveFollowEdge_ = false;
    computeInitialViewport_();
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToStart() {
    markUserViewportControl_();
    const qint64 w = tsMax_ - tsMin_;
    tsMin_ = replay_.firstTsNs();
    tsMax_ = tsMin_ + w;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToEnd() {
    liveFollowEdge_ = false;
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
    return formatShortTimeNs(value, std::max<std::int64_t>(span, 1));
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
    const auto span = std::max<std::int64_t>(tsMax_ - tsMin_, 1);
    const auto step = niceTimeStepNs(span, safeTicks);
    const auto start = floorToStep(tsMin_, step);
    const auto value = start + static_cast<std::int64_t>(index) * step;
    return formatShortTimeNs(value, span);
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
    snap.loaded = loaded_
        || !liveDataCache_.stableRows.trades.empty()
        || !liveDataCache_.stableRows.liquidations.empty()
        || !liveDataCache_.stableRows.bookTickers.empty()
        || !liveDataCache_.stableRows.depths.empty()
        || !liveDataCache_.overlayRows.trades.empty()
        || !liveDataCache_.overlayRows.liquidations.empty()
        || !liveDataCache_.overlayRows.bookTickers.empty()
        || !liveDataCache_.overlayRows.depths.empty()
        || !replay_.candles().empty();
    snap.tradesVisible = in.tradesVisible;
    snap.liquidationsVisible = in.liquidationsVisible;
    snap.candlesVisible = in.candlesVisible;
    snap.orderbookVisible = in.orderbookVisible;
    snap.bookTickerVisible = in.bookTickerVisible;
    snap.tradeConnectorsVisible = in.tradesVisible;
    snap.interactiveMode = in.interactiveMode;
    snap.overlayOnly = in.overlayOnly;
    snap.exactTradeRendering = in.exactTradeRendering;
    snap.tradeAmountScale = in.tradeAmountScale;
    snap.candleWidthPx = in.candleWidthPx;
    snap.bookOpacityGain = in.bookOpacityGain;
    snap.bookRenderDetail = in.bookRenderDetail;
    snap.bookDepthWindowPct = std::clamp<qreal>(in.bookDepthWindowPct, 1.0, 25.0);
    const bool buildGpuOrderbookVertices = in.gpuOrderbookVertices;
    snap.verticalMarkers = verticalMarkers_;

    if (!snap.loaded || widthPx <= 0.0 || heightPx <= 0.0) return snap;
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return snap;

    const bool latestOnlyWindow = latestOnlyRenderWindow_();
    const std::int64_t latestTsNs = latestRenderableTsNs_();
    const std::int64_t renderMinTs = latestOnlyWindow
        ? latestTsNs
        : (limitedRenderWindow_() ? effectiveRenderMinTs_(latestTsNs) : snap.vp.tMin);
    const std::int64_t renderMaxTs = (latestOnlyWindow || limitedRenderWindow_()) && latestTsNs > 0
        ? std::min<std::int64_t>(snap.vp.tMax, latestTsNs)
        : snap.vp.tMax;
    if ((latestOnlyWindow || limitedRenderWindow_()) && (latestTsNs <= 0 || renderMaxTs < renderMinTs)) return snap;

    if (!strategyOverlay_.empty()) {
        snap.strategyOrderSegments.reserve(strategyOverlay_.orderSegments.size());
        for (const auto& segment : strategyOverlay_.orderSegments) {
            if (segment.tsEndNs < renderMinTs || segment.tsStartNs > renderMaxTs) continue;
            if (segment.priceE8 < snap.vp.pMin || segment.priceE8 > snap.vp.pMax) continue;
            snap.strategyOrderSegments.push_back(segment);
        }
        snap.strategyFillMarkers.reserve(strategyOverlay_.fillMarkers.size());
        for (const auto& marker : strategyOverlay_.fillMarkers) {
            if (marker.tsNs < renderMinTs || marker.tsNs > renderMaxTs) continue;
            if (marker.priceE8 < snap.vp.pMin || marker.priceE8 > snap.vp.pMax) continue;
            const auto x = snap.vp.toX(marker.tsNs);
            const auto y = snap.vp.toY(marker.priceE8);
            if (x < -12.0 || x > snap.vp.w + 12.0 || y < -12.0 || y > snap.vp.h + 12.0) continue;
            snap.strategyFillMarkers.push_back(marker);
        }
    }

    const auto minVisibleAmountE8 = usdToE8Min0(in.bookRenderDetail);
    if (in.tradesVisible) {
        const auto& trades = replay_.trades();
        auto tradeBegin = trades.begin();
        auto tradeEnd = trades.end();
        if (latestOnlyWindow) {
            const auto latestTradeIt = std::upper_bound(
                trades.begin(),
                trades.end(),
                latestTsNs,
                [](std::int64_t ts, const hftrec::replay::TradeRow& row) noexcept { return ts < row.tsNs; });
            if (latestTradeIt == trades.begin()) {
                tradeBegin = trades.end();
                tradeEnd = trades.end();
            } else {
                tradeBegin = std::prev(latestTradeIt);
                tradeEnd = latestTradeIt;
            }
        } else {
            tradeBegin = std::lower_bound(
                trades.begin(),
                trades.end(),
                renderMinTs,
                [](const hftrec::replay::TradeRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
            tradeEnd = std::upper_bound(
                tradeBegin,
                trades.end(),
                renderMaxTs,
                [](std::int64_t ts, const hftrec::replay::TradeRow& row) noexcept { return ts < row.tsNs; });
        }

        const auto visible = [&](const hftrec::replay::TradeRow& trade) noexcept {
            if (latestOnlyWindow && (trade.tsNs < snap.vp.tMin || trade.tsNs > snap.vp.tMax)) return false;
            if (trade.priceE8 < snap.vp.pMin || trade.priceE8 > snap.vp.pMax) return false;
            const auto x = snap.vp.toX(trade.tsNs);
            const auto y = snap.vp.toY(trade.priceE8);
            return x >= 0.0 && x <= snap.vp.w && y >= 0.0 && y <= snap.vp.h;
        };

        std::size_t visibleTradeDotCount = 0;
        bool hasPreviousVisibleDot = false;
        std::int64_t previousDotTsNs = 0;
        std::int64_t previousDotPriceE8 = 0;
        bool previousDotSideBuy = false;
        for (auto it = tradeBegin; it != tradeEnd; ++it) {
            if (!visible(*it)) continue;
            const auto& t = *it;
            const bool sideBuy = t.sideBuy != 0;
            if (!hasPreviousVisibleDot
                || previousDotTsNs != t.tsNs
                || previousDotPriceE8 != t.priceE8
                || previousDotSideBuy != sideBuy) {
                ++visibleTradeDotCount;
                hasPreviousVisibleDot = true;
                previousDotTsNs = t.tsNs;
                previousDotPriceE8 = t.priceE8;
                previousDotSideBuy = sideBuy;
            }
        }

        const auto lod = decideTradeLod(visibleTradeDotCount, widthPx, in.exactTradeRendering, tradeLodAggregated_);
        tradeLodAggregated_ = lod.aggregate;
        snap.tradeDecimated = lod.aggregate;
        snap.tradeConnectorsVisible = snap.tradeConnectorsVisible && !snap.tradeDecimated;
        snap.tradeDots.reserve(snap.tradeDecimated
            ? std::max<std::size_t>(1u, static_cast<std::size_t>(std::ceil(widthPx)))
            : visibleTradeDotCount);

        if (!snap.tradeDecimated) {
            for (auto it = tradeBegin; it != tradeEnd; ++it) {
                const auto& t = *it;
                if (!visible(t)) continue;
                const auto origIndex = static_cast<int>(std::distance(trades.begin(), it));
                TradeDot dot{};
                dot.tsNs = t.tsNs;
                dot.priceE8 = t.priceE8;
                dot.qtyE8 = t.qtyE8;
                dot.sideBuy = t.sideBuy != 0;
                dot.origIndex = origIndex;
                detail::appendGroupedTradeDot(snap.tradeDots, std::move(dot));
            }
        } else {
            TradePixelBucket bucket{};
            const auto flushTradeBucket = [&]() { bucket.appendTo(snap.tradeDots); };
            for (auto it = tradeBegin; it != tradeEnd; ++it) {
                const auto& t = *it;
                if (!visible(t)) continue;
                const int xPx = std::clamp(
                    static_cast<int>(std::floor(snap.vp.toX(t.tsNs))),
                    0,
                    std::max(0, static_cast<int>(std::ceil(widthPx)) - 1));
                if (bucket.xPx != xPx) {
                    flushTradeBucket();
                    bucket.reset(xPx);
                }
                const auto origIndex = static_cast<int>(std::distance(trades.begin(), it));
                bucket.absorb(t.tsNs, t.priceE8, t.qtyE8, t.sideBuy != 0, origIndex);
            }
            flushTradeBucket();
        }
    }

    if (in.liquidationsVisible) {
        const auto& liquidations = replay_.liquidations();
        auto liqBegin = liquidations.begin();
        auto liqEnd = liquidations.end();
        if (latestOnlyWindow) {
            const auto latestLiqIt = std::upper_bound(
                liquidations.begin(),
                liquidations.end(),
                latestTsNs,
                [](std::int64_t ts, const hftrec::replay::LiquidationRow& row) noexcept { return ts < row.tsNs; });
            if (latestLiqIt == liquidations.begin()) {
                liqBegin = liquidations.end();
                liqEnd = liquidations.end();
            } else {
                liqBegin = std::prev(latestLiqIt);
                liqEnd = latestLiqIt;
            }
        } else {
            liqBegin = std::lower_bound(
                liquidations.begin(),
                liquidations.end(),
                renderMinTs,
                [](const hftrec::replay::LiquidationRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
            liqEnd = std::upper_bound(
                liqBegin,
                liquidations.end(),
                renderMaxTs,
                [](std::int64_t ts, const hftrec::replay::LiquidationRow& row) noexcept { return ts < row.tsNs; });
        }
        snap.liquidationDots.reserve(static_cast<std::size_t>(std::distance(liqBegin, liqEnd)));
        for (auto it = liqBegin; it != liqEnd; ++it) {
            const auto& row = *it;
            if (latestOnlyWindow && (row.tsNs < snap.vp.tMin || row.tsNs > snap.vp.tMax)) continue;
            if (row.priceE8 < snap.vp.pMin || row.priceE8 > snap.vp.pMax) continue;
            const auto x = snap.vp.toX(row.tsNs);
            const auto y = snap.vp.toY(row.priceE8);
            if (x < 0.0 || x > snap.vp.w || y < 0.0 || y > snap.vp.h) continue;
            const auto origIndex = static_cast<int>(std::distance(liquidations.begin(), it));
            snap.liquidationDots.push_back(LiquidationDot{row.tsNs, row.priceE8, row.qtyE8, row.avgPriceE8, row.filledQtyE8, row.sideBuy != 0, origIndex});
        }
    }

    if (in.candlesVisible) {
        const auto& candles = replay_.candles();
        std::int64_t previousMidByTier[4]{};
        bool hasPreviousByTier[4]{};
        auto tierDurationNs = [](std::int64_t tier) noexcept -> std::int64_t {
            if (tier == 1) return 60ll * 1000000000ll;
            if (tier == 2) return 15ll * 60ll * 1000000000ll;
            return 24ll * 60ll * 60ll * 1000000000ll;
        };
        snap.candleRects.reserve(candles.size());
        for (const auto& row : candles) {
            if (row.tier < 1 || row.tier > 3) continue;
            const auto tierIndex = static_cast<std::size_t>(row.tier);
            const std::int64_t mid = row.lowE8 + (row.highE8 - row.lowE8) / 2;
            const bool up = !hasPreviousByTier[tierIndex] || mid >= previousMidByTier[tierIndex];
            previousMidByTier[tierIndex] = mid;
            hasPreviousByTier[tierIndex] = true;

            const std::int64_t durationNs = tierDurationNs(row.tier);
            if ((row.tsNs + durationNs) < renderMinTs || row.tsNs > renderMaxTs) continue;
            if (row.highE8 < snap.vp.pMin || row.lowE8 > snap.vp.pMax) continue;

            const qreal x = snap.vp.toX(row.tsNs);
            const qreal w = std::clamp(snap.candleWidthPx, 1.0, 80.0);
            const qreal yHigh = snap.vp.toY(row.highE8);
            const qreal yLow = snap.vp.toY(row.lowE8);
            qreal y = std::min(yHigh, yLow);
            qreal h = std::max<qreal>(2.0, std::abs(yLow - yHigh));
            if ((x + w) < 0.0 || x > snap.vp.w || (y + h) < 0.0 || y > snap.vp.h) continue;
            snap.candleRects.push_back(CandleRect{row.tier, row.tsNs, row.highE8, row.lowE8, row.quoteAmountE8, x, y, w, h, up});
        }
    }

    if (!in.orderbookVisible && !in.bookTickerVisible) return snap;

    const auto& tickers = replay_.bookTickers();
    if (in.bookTickerVisible) {
        if (latestOnlyWindow) {
            buildLatestBookTickerTrace(snap, tickers, latestTsNs);
        } else {
            const std::int64_t tickerRenderMax = tickers.empty()
                ? renderMaxTs
                : std::min<std::int64_t>(renderMaxTs, tickers.back().tsNs);
            buildBookTickerTrace(snap, tickers, renderMinTs, tickerRenderMax);
        }
    }
    if (!in.orderbookVisible) return snap;

    const std::int64_t orderbookLatestTsNs = latestOnlyWindow ? latestOrderbookTsNs_() : latestTsNs;
    const std::int64_t orderbookRenderMinTs = latestOnlyWindow
        ? orderbookLatestTsNs
        : (limitedRenderWindow_() ? effectiveRenderMinTs_(orderbookLatestTsNs) : snap.vp.tMin);
    const std::int64_t coverageStart = std::max<std::int64_t>(
        latestOnlyWindow ? orderbookLatestTsNs : orderbookRenderMinTs,
        replay_.firstTsNs());
    const std::int64_t coverageEnd = std::min<std::int64_t>(snap.vp.tMax, latestOnlyWindow ? orderbookLatestTsNs + 1 : replay_.lastTsNs());
    if (coverageEnd <= coverageStart) return snap;

    replay_.seek(coverageStart);
    const auto& buckets = replay_.buckets();

    const auto brightnessRefE8 = usdToE8(in.bookOpacityGain);

    const double minOrderbookSegmentWidthPx = in.interactiveMode
        ? kBookMinSegmentWidthPxInteractive
        : kBookMinSegmentWidthPxStatic;

    auto emitSegment = [&](std::int64_t tsStart,
                           std::int64_t tsEnd) {
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
            const BookTickerAnchor anchor = bookTickerAnchorAtOrBefore(tickers, tsStart);
            const std::int64_t anchorBidE8 = anchor.bidPriceE8 > 0 ? anchor.bidPriceE8 : book.bestBidPrice();
            const std::int64_t anchorAskE8 = anchor.askPriceE8 > 0 ? anchor.askPriceE8 : book.bestAskPrice();
            const std::int64_t bidMinE8 = windowBidMinE8(anchorBidE8, snap.bookDepthWindowPct);
            const std::int64_t askMaxE8 = windowAskMaxE8(anchorAskE8, snap.bookDepthWindowPct);
            std::size_t keptBids = 0;
            std::size_t keptAsks = 0;
            int lastBidYPx = std::numeric_limits<int>::min();
            int lastAskYPx = std::numeric_limits<int>::min();
            const std::size_t levelsBudget = in.interactiveMode
                ? kInteractiveBookLevelsBudgetPerSide
                : kRenderBookLevelsBudgetPerSide;
            const std::size_t candidateBudget = bookLevelCandidateBudget(levelsBudget);
            const std::int64_t bidFilterMinE8 = std::max<std::int64_t>(snap.vp.pMin, bidMinE8);
            const std::int64_t askFilterMaxE8 = askMaxE8 > 0
                ? std::min<std::int64_t>(snap.vp.pMax, askMaxE8)
                : snap.vp.pMax;
            const auto bidLevels = book.filteredBids(bidFilterMinE8, snap.vp.pMax, candidateBudget);
            const auto askLevels = book.filteredAsks(snap.vp.pMin, askFilterMaxE8, candidateBudget);
            for (const auto& [price, qty] : bidLevels) {
                if (keptBids >= levelsBudget) break;
                if (bidMinE8 > 0 && price < bidMinE8) break;
                int yPx = 0;
                std::uint8_t alpha = 0;
                if (!prepareVisibleLevelForScreen(
                        BookLevel{price, qty},
                        snap.vp,
                        minVisibleAmountE8,
                        brightnessRefE8,
                        yPx,
                        alpha)) {
                    continue;
                }
                if (yPx == lastBidYPx) continue;
                seg.bids.push_back(BookLevel{price, qty, alpha});
                if (qty > maxBid) maxBid = qty;
                ++keptBids;
                lastBidYPx = yPx;
            }
            for (const auto& [price, qty] : askLevels) {
                if (keptAsks >= levelsBudget) break;
                if (askMaxE8 > 0 && price > askMaxE8) break;
                int yPx = 0;
                std::uint8_t alpha = 0;
                if (!prepareVisibleLevelForScreen(
                        BookLevel{price, qty},
                        snap.vp,
                        minVisibleAmountE8,
                        brightnessRefE8,
                        yPx,
                        alpha)) {
                    continue;
                }
                if (yPx == lastAskYPx) continue;
                seg.asks.push_back(BookLevel{price, qty, alpha});
                if (qty > maxAsk) maxAsk = qty;
                ++keptAsks;
                lastAskYPx = yPx;
            }
            seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
            seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
        }
        if (seg.bids.empty() && seg.asks.empty()) return;
        if (buildGpuOrderbookVertices) {
            appendGpuVerticesForSide(
                snap.gpuBookVertices,
                seg.bids,
                snap.vp,
                xLeft,
                xRight,
                bidColor(),
                brightnessRefE8,
                minVisibleAmountE8);
            appendGpuVerticesForSide(
                snap.gpuBookVertices,
                seg.asks,
                snap.vp,
                xLeft,
                xRight,
                askColor(),
                brightnessRefE8,
                minVisibleAmountE8);
        }
        snap.bookSegments.push_back(std::move(seg));
    };

    std::int64_t segStart = coverageStart;
    std::size_t bucketCursor = replay_.cursor();
    while (bucketCursor < buckets.size() && buckets[bucketCursor].tsNs <= coverageEnd) {
        const auto stampTs = buckets[bucketCursor].tsNs;
        const double xStart = snap.vp.toX(segStart);
        const double xStamp = snap.vp.toX(stampTs);
        const bool wide = (xStamp - xStart) >= minOrderbookSegmentWidthPx;

        if (wide) {
            emitSegment(segStart, stampTs);
            segStart = stampTs;
        }
        replay_.seek(stampTs);
        bucketCursor = replay_.cursor();
    }
    emitSegment(segStart, coverageEnd);

    return snap;
}

}  // namespace hftrec::gui::viewer




