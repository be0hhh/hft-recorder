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
#include "gui/viewer/detail/BookMath.hpp"
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
    const qreal clamped = std::clamp<qreal>(usd, 1000.0, 1000000.0);
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

struct BookTickerPixelState {
    bool has{false};
    std::int64_t firstPriceE8{0};
    std::int64_t lastPriceE8{0};
    std::int64_t minPriceE8{0};
    std::int64_t maxPriceE8{0};
    std::int64_t lastQtyE8{0};
    std::int64_t lastTsNs{0};

    void absorb(std::int64_t priceE8, std::int64_t qtyE8, std::int64_t tsNs) noexcept {
        if (priceE8 <= 0) return;
        if (!has) {
            has = true;
            firstPriceE8 = priceE8;
            minPriceE8 = priceE8;
            maxPriceE8 = priceE8;
        } else {
            minPriceE8 = std::min(minPriceE8, priceE8);
            maxPriceE8 = std::max(maxPriceE8, priceE8);
        }
        lastPriceE8 = priceE8;
        lastQtyE8 = qtyE8;
        lastTsNs = tsNs;
    }
};

bool visiblyDifferent(qreal lhs, qreal rhs) noexcept {
    return std::abs(lhs - rhs) >= 0.5;
}

constexpr std::int64_t kBookTickerStaleGapNs = 1'000'000'000ll;
constexpr std::int64_t kOrderbookStaleGapNs = 500'000'000ll;
constexpr std::int64_t kOrderbookMaxHoldPixels = 12ll;

bool bucketHasDepth(const hftrec::replay::SessionReplay::ReplayBucket& bucket) noexcept {
    return std::any_of(bucket.items.begin(), bucket.items.end(), [](const auto& item) noexcept {
        return item.kind == hftrec::replay::SessionReplay::EventKind::Depth;
    });
}

std::int64_t latestDepthTsAtOrBefore(const std::vector<hftrec::replay::DepthRow>& depths,
                                     std::int64_t tsNs) noexcept {
    const auto it = std::upper_bound(
        depths.begin(),
        depths.end(),
        tsNs,
        [](std::int64_t ts, const hftrec::replay::DepthRow& row) noexcept {
            return ts < row.tsNs;
        });
    if (it == depths.begin()) return 0;
    return std::prev(it)->tsNs;
}

void appendBookTickerSideLines(std::vector<BookTickerLine>& out,
                               const std::vector<BookTickerPixelState>& pixels,
                               const ViewportMap& vp) {
    bool prevHas = false;
    int prevPx = -1;
    qreal prevLastY = 0.0;

    for (int px = 0; px < static_cast<int>(pixels.size()); ++px) {
        const auto& state = pixels[static_cast<std::size_t>(px)];
        if (!state.has) {
            prevHas = false;
            prevPx = -1;
            continue;
        }

        const qreal x0 = static_cast<qreal>(px);
        const qreal x1 = std::min<qreal>(x0 + 1.0, vp.w);
        const qreal firstY = vp.toY(state.firstPriceE8);
        const qreal lastY = vp.toY(state.lastPriceE8);

        if (prevHas && prevPx + 1 == px && visiblyDifferent(prevLastY, firstY)) {
            out.push_back(BookTickerLine{x0, prevLastY, x0, firstY});
        }

        if (state.minPriceE8 != state.maxPriceE8) {
            const qreal y0 = vp.toY(state.minPriceE8);
            const qreal y1 = vp.toY(state.maxPriceE8);
            if (visiblyDifferent(y0, y1)) {
                out.push_back(BookTickerLine{x0, y0, x0, y1});
            }
        }

        if (x1 > x0) {
            out.push_back(BookTickerLine{x0, lastY, x1, lastY});
        }

        prevHas = true;
        prevPx = px;
        prevLastY = lastY;
    }
}

void absorbBookTickerInterval(std::vector<BookTickerPixelState>& bidPixels,
                              std::vector<BookTickerPixelState>& askPixels,
                              const ViewportMap& vp,
                              const hftrec::replay::BookTickerRow& ticker,
                              std::int64_t tsStart,
                              std::int64_t tsEnd) {
    if (tsEnd <= tsStart || bidPixels.empty() || askPixels.empty()) return;

    const qreal xLeft = std::clamp(vp.toX(tsStart), 0.0, vp.w);
    const qreal xRight = std::clamp(vp.toX(tsEnd), 0.0, vp.w);
    if (xRight <= xLeft) return;

    const int maxPx = static_cast<int>(bidPixels.size()) - 1;
    const int startPx = std::clamp(static_cast<int>(std::floor(xLeft)), 0, maxPx);
    const int endPx = std::clamp(static_cast<int>(std::ceil(xRight)) - 1, 0, maxPx);
    if (endPx < startPx) return;

    for (int px = startPx; px <= endPx; ++px) {
        bidPixels[static_cast<std::size_t>(px)].absorb(ticker.bidPriceE8, ticker.bidQtyE8, ticker.tsNs);
        askPixels[static_cast<std::size_t>(px)].absorb(ticker.askPriceE8, ticker.askQtyE8, ticker.tsNs);
    }
}

void buildBookTickerTrace(RenderSnapshot& snap,
                          const std::vector<hftrec::replay::BookTickerRow>& tickers,
                          std::int64_t renderMinTs,
                          std::int64_t renderMaxTs) {
    snap.bookTickerTrace = BookTickerTrace{};
    if (!snap.bookTickerVisible || tickers.empty() || snap.vp.w <= 0.0) return;
    renderMinTs = std::max<std::int64_t>(renderMinTs, snap.vp.tMin);
    renderMaxTs = std::min<std::int64_t>(renderMaxTs, snap.vp.tMax);
    if (renderMaxTs < renderMinTs) return;

    const int widthPx = std::max(1, static_cast<int>(std::ceil(snap.vp.w)));
    const std::int64_t pixelSpanNs = std::max<std::int64_t>(
        1,
        (snap.vp.tMax - snap.vp.tMin + widthPx - 1) / widthPx);
    std::vector<BookTickerPixelState> bidPixels(static_cast<std::size_t>(widthPx));
    std::vector<BookTickerPixelState> askPixels(static_cast<std::size_t>(widthPx));

    const auto firstAfterStart = std::upper_bound(
        tickers.begin(),
        tickers.end(),
        renderMinTs,
        [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept {
            return ts < row.tsNs;
        });

    std::size_t index = 0;
    if (firstAfterStart != tickers.begin()) {
        index = static_cast<std::size_t>(std::distance(tickers.begin(), std::prev(firstAfterStart)));
    } else {
        index = static_cast<std::size_t>(std::distance(tickers.begin(), firstAfterStart));
    }

    while (index < tickers.size()) {
        const auto& ticker = tickers[index];
        if (ticker.tsNs > renderMaxTs) break;

        const bool hasNext = (index + 1u < tickers.size());
        if (!hasNext && ticker.tsNs < renderMinTs) break;

        const std::int64_t tsStart = hasNext
            ? std::max<std::int64_t>(renderMinTs, ticker.tsNs)
            : ticker.tsNs;
        std::int64_t nextTs = hasNext ? tickers[index + 1u].tsNs : (ticker.tsNs + pixelSpanNs);
        if (hasNext && nextTs - ticker.tsNs > kBookTickerStaleGapNs) {
            nextTs = ticker.tsNs + pixelSpanNs;
        }
        const std::int64_t tsEnd = std::min<std::int64_t>(renderMaxTs, nextTs);
        absorbBookTickerInterval(bidPixels, askPixels, snap.vp, ticker, tsStart, tsEnd);
        if (!hasNext || nextTs >= renderMaxTs) break;
        ++index;
    }

    auto& trace = snap.bookTickerTrace;
    trace.bidLines.reserve(static_cast<std::size_t>(widthPx) * 2u);
    trace.askLines.reserve(static_cast<std::size_t>(widthPx) * 2u);
    trace.samples.reserve(static_cast<std::size_t>(widthPx));

    appendBookTickerSideLines(trace.bidLines, bidPixels, snap.vp);
    appendBookTickerSideLines(trace.askLines, askPixels, snap.vp);

    for (int px = 0; px < widthPx; ++px) {
        const auto& bid = bidPixels[static_cast<std::size_t>(px)];
        const auto& ask = askPixels[static_cast<std::size_t>(px)];
        if (!bid.has && !ask.has) continue;
        trace.samples.push_back(BookTickerSample{
            px,
            std::max(bid.lastTsNs, ask.lastTsNs),
            bid.has ? bid.lastPriceE8 : 0,
            bid.has ? bid.lastQtyE8 : 0,
            ask.has ? ask.lastPriceE8 : 0,
            ask.has ? ask.lastQtyE8 : 0,
        });
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

    const int widthPx = std::max(1, static_cast<int>(std::ceil(snap.vp.w)));
    const int xPx = std::clamp(
        static_cast<int>(std::round(snap.vp.toX(ticker.tsNs))),
        0,
        widthPx - 1);
    auto appendSampleLine = [&](std::int64_t priceE8, std::vector<BookTickerLine>& out) {
        if (priceE8 <= 0 || priceE8 < snap.vp.pMin || priceE8 > snap.vp.pMax) return;
        const qreal y = snap.vp.toY(priceE8);
        out.push_back(BookTickerLine{static_cast<qreal>(xPx), y, static_cast<qreal>(xPx + 1), y});
    };
    appendSampleLine(ticker.bidPriceE8, snap.bookTickerTrace.bidLines);
    appendSampleLine(ticker.askPriceE8, snap.bookTickerTrace.askLines);
    if (!snap.bookTickerTrace.bidLines.empty() || !snap.bookTickerTrace.askLines.empty()) {
        snap.bookTickerTrace.samples.push_back(BookTickerSample{
            xPx,
            ticker.tsNs,
            ticker.bidPriceE8,
            ticker.bidQtyE8,
            ticker.askPriceE8,
            ticker.askQtyE8,
        });
    }
}

}  // namespace

void ChartController::computeInitialViewport_() {
    tsMin_ = replay_.firstTsNs();
    tsMax_ = replay_.lastTsNs();
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
        || !liveDataCache_.stableRows.bookTickers.empty()
        || !liveDataCache_.stableRows.depths.empty()
        || !liveDataCache_.overlayRows.trades.empty()
        || !liveDataCache_.overlayRows.bookTickers.empty()
        || !liveDataCache_.overlayRows.depths.empty();
    snap.tradesVisible = in.tradesVisible;
    snap.orderbookVisible = in.orderbookVisible;
    snap.bookTickerVisible = in.bookTickerVisible;
    snap.interactiveMode = in.interactiveMode;
    snap.overlayOnly = in.overlayOnly;
    snap.exactTradeRendering = in.exactTradeRendering;
    snap.tradeAmountScale = in.tradeAmountScale;
    snap.bookOpacityGain = in.bookOpacityGain;
    snap.bookRenderDetail = in.bookRenderDetail;
    snap.bookDepthWindowPct = std::clamp<qreal>(in.bookDepthWindowPct, 1.0, 25.0);
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

    const auto minVisibleAmountE8 = usdToE8(in.bookRenderDetail);
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

    std::size_t visibleTradeCount = 0;
    for (auto it = tradeBegin; it != tradeEnd; ++it) {
        const auto& trade = *it;
        if (latestOnlyWindow) {
            if (trade.tsNs < snap.vp.tMin || trade.tsNs > snap.vp.tMax) continue;
        }
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

    for (auto it = tradeBegin; it != tradeEnd; ++it) {
        const auto& t = *it;
        if (latestOnlyWindow) {
            if (t.tsNs < snap.vp.tMin || t.tsNs > snap.vp.tMax) continue;
        }
        if (t.priceE8 < snap.vp.pMin || t.priceE8 > snap.vp.pMax) continue;
        const auto x = snap.vp.toX(t.tsNs);
        const auto y = snap.vp.toY(t.priceE8);
        if (x < 0.0 || x > snap.vp.w || y < 0.0 || y > snap.vp.h) continue;

        const auto origIndex = static_cast<int>(std::distance(trades.begin(), it));
        const TradeDot dot{t.tsNs, t.priceE8, t.qtyE8, t.sideBuy != 0, origIndex};
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

    const auto& tickers = replay_.bookTickers();
    if (in.bookTickerVisible) {
        if (latestOnlyWindow) buildLatestBookTickerTrace(snap, tickers, latestTsNs);
        else buildBookTickerTrace(snap, tickers, renderMinTs, renderMaxTs);
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
    const auto& depths = replay_.depths();
    const bool hasDepthRows = !depths.empty();
    const std::int64_t widthPxInt = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(widthPx)));
    const std::int64_t pixelSpanNs = std::max<std::int64_t>(
        1,
        (snap.vp.tMax - snap.vp.tMin + widthPxInt - 1) / widthPxInt);
    const std::int64_t orderbookHoldNs = std::min<std::int64_t>(
        kOrderbookStaleGapNs,
        std::max<std::int64_t>(pixelSpanNs, pixelSpanNs * kOrderbookMaxHoldPixels));
    std::int64_t lastDepthTs = latestDepthTsAtOrBefore(depths, coverageStart);

    const auto brightnessRefE8 = usdToE8(in.bookOpacityGain);

    auto emitSegment = [&](std::int64_t tsStart,
                           std::int64_t tsEnd) {
        if (tsEnd <= tsStart) return;
        if (hasDepthRows && lastDepthTs > 0) {
            const std::int64_t staleEnd = lastDepthTs + orderbookHoldNs;
            tsEnd = std::min(tsEnd, staleEnd);
            if (tsEnd <= tsStart) return;
        }
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
            const std::int64_t bidMinE8 = windowBidMinE8(book.bestBidPrice(), snap.bookDepthWindowPct);
            const std::int64_t askMaxE8 = windowAskMaxE8(book.bestAskPrice(), snap.bookDepthWindowPct);
            std::size_t keptBids = 0;
            std::size_t keptAsks = 0;
            int lastBidYPx = std::numeric_limits<int>::min();
            int lastAskYPx = std::numeric_limits<int>::min();
            const std::size_t levelsBudget = in.interactiveMode
                ? kInteractiveBookLevelsBudgetPerSide
                : kRenderBookLevelsBudgetPerSide;
            for (const auto& [price, qty] : book.bids()) {
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
                seg.bids.push_back(BookLevel{price, qty});
                if (qty > maxBid) maxBid = qty;
                ++keptBids;
                lastBidYPx = yPx;
            }
            for (const auto& [price, qty] : book.asks()) {
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
                seg.asks.push_back(BookLevel{price, qty});
                if (qty > maxAsk) maxAsk = qty;
                ++keptAsks;
                lastAskYPx = yPx;
            }
            seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
            seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
        }
        if (seg.bids.empty() && seg.asks.empty()) return;
        if (in.orderbookVisible) {
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
        const bool depthBucket = bucketHasDepth(buckets[bucketCursor]);
        const auto stampTs = buckets[bucketCursor].tsNs;
        const double xStart = snap.vp.toX(segStart);
        const double xStamp = snap.vp.toX(stampTs);
        const bool wide = (xStamp - xStart) >= 1.0;

        if (wide) {
            emitSegment(segStart, stampTs);
            segStart = stampTs;
        }
        replay_.seek(stampTs);
        if (depthBucket) lastDepthTs = stampTs;
        bucketCursor = replay_.cursor();
    }
    emitSegment(segStart, coverageEnd);

    return snap;
}

}  // namespace hftrec::gui::viewer


