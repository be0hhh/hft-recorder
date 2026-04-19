#include "gui/viewer/renderers/BookRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QPen>

#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::renderers {

namespace {

constexpr std::int64_t kUsdScaleE8 = 100000000ll;
constexpr int kHardLevelBudgetPerSide = 256;

std::int64_t usdToE8(qreal usd) noexcept {
    const qreal clamped = std::clamp<qreal>(usd, 100.0, 100000.0);
    return static_cast<std::int64_t>(std::llround(clamped * static_cast<qreal>(kUsdScaleE8)));
}

bool isVisibleLevel(const BookLevel& level,
                    const ViewportMap& vp,
                    std::int64_t minVisibleAmountE8,
                    std::int64_t& outAmountE8) noexcept {
    if (level.qtyE8 <= 0 || level.priceE8 < vp.pMin || level.priceE8 > vp.pMax) return false;
    outAmountE8 = detail::multiplyScaledE8(level.qtyE8, level.priceE8);
    return outAmountE8 >= minVisibleAmountE8;
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

void drawSide(QPainter* painter,
              const std::vector<BookLevel>& levels,
              const ViewportMap& vp,
              qreal xLeft, qreal xRight,
              const QColor& baseColor,
              std::int64_t brightnessRefE8,
              std::int64_t minVisibleAmountE8) {
    if (xRight <= xLeft || levels.empty()) return;

    const int xStart   = static_cast<int>(std::floor(xLeft));
    const int xEnd     = std::max(xStart + 1, static_cast<int>(std::ceil(xRight)));
    const int width    = xEnd - xStart;
    const int heightPx = static_cast<int>(std::ceil(vp.h));

    int seen = 0;
    for (const auto& level : levels) {
        if (seen >= kHardLevelBudgetPerSide) break;
        std::int64_t amountE8 = 0;
        if (!isVisibleLevel(level, vp, minVisibleAmountE8, amountE8)) continue;
        ++seen;

        const int y = std::clamp(static_cast<int>(std::round(vp.toY(level.priceE8))), 0, heightPx - 1);
        const qreal brightness = normalizedBrightness(amountE8, minVisibleAmountE8, brightnessRefE8);
        const int alpha = std::clamp(static_cast<int>(std::round(brightness * 255.0)), 0, 255);
        if (alpha <= 1) continue;
        QColor color = baseColor;
        color.setAlpha(alpha);
        QPen pen(color);
        pen.setWidth(1);
        pen.setCapStyle(Qt::SquareCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(xStart, y, xEnd - 1, y);
    }
}

}  // namespace

void renderBook(const RenderContext& ctx) {
    if (!ctx.s.orderbookVisible) return;

    const auto brightnessRefE8 = usdToE8(ctx.s.bookOpacityGain);
    const auto minVisibleAmountE8 = usdToE8(ctx.s.bookRenderDetail);

    for (const auto& seg : ctx.s.bookSegments) {
        const qreal xLeft  = std::clamp(ctx.s.vp.toX(seg.tsStartNs), 0.0, ctx.s.vp.w);
        const qreal xRight = std::clamp(ctx.s.vp.toX(seg.tsEndNs),   0.0, ctx.s.vp.w);
        if (xRight <= xLeft) continue;

        drawSide(ctx.p, seg.bids, ctx.s.vp, xLeft, xRight, bidColor(), brightnessRefE8, minVisibleAmountE8);
        drawSide(ctx.p, seg.asks, ctx.s.vp, xLeft, xRight, askColor(), brightnessRefE8, minVisibleAmountE8);
    }
}

}  // namespace hftrec::gui::viewer::renderers
