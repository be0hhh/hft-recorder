#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"

namespace hftrec::gui::viewer::detail {

struct BookTickerTraceSideState {
    bool active{false};
    qreal x{0.0};
    qreal y{0.0};
};

struct BookTickerTraceBuildState {
    BookTickerTraceSideState bid{};
    BookTickerTraceSideState ask{};
};

inline std::int64_t bookTickerTraceTimestampAtX(const ViewportMap& vp, int xPx) noexcept {
    if (vp.tMax <= vp.tMin || vp.w <= 0.0) return vp.tMin;
    const double clampedX = std::clamp(static_cast<double>(xPx), 0.0, vp.w);
    const double span = static_cast<double>(vp.tMax - vp.tMin);
    return vp.tMin + static_cast<std::int64_t>(std::llround((clampedX / vp.w) * span));
}

inline bool bookTickerTraceVisiblePrice(const ViewportMap& vp, std::int64_t priceE8) noexcept {
    return priceE8 > 0 && priceE8 >= vp.pMin && priceE8 <= vp.pMax;
}

inline qreal bookTickerTraceRoundedX(const ViewportMap& vp, std::int64_t tsNs) noexcept {
    return static_cast<qreal>(std::round(std::clamp(vp.toX(tsNs), 0.0, vp.w)));
}

inline qreal bookTickerTraceRoundedY(const ViewportMap& vp, std::int64_t priceE8) noexcept {
    return static_cast<qreal>(std::round(std::clamp(vp.toY(priceE8), 0.0, vp.h)));
}

inline void appendBookTickerTraceLine(std::vector<BookTickerLine>& lines,
                                      BookTickerTraceSideState& state,
                                      qreal x0,
                                      qreal x1,
                                      qreal y) {
    if (state.active && x0 < state.x) x0 = state.x;
    if (x1 <= x0) x1 = x0 + 1.0;
    if (state.active && state.x < x0) {
        lines.push_back(BookTickerLine{state.x, state.y, x0, state.y});
    }
    if (state.active && state.y != y) {
        lines.push_back(BookTickerLine{x0, state.y, x0, y});
    }
    lines.push_back(BookTickerLine{x0, y, x1, y});
    state.active = true;
    state.x = x1;
    state.y = y;
}

template <typename BookTickerRowLike>
inline void appendBookTickerTraceSamples(BookTickerTrace& trace,
                                         const ViewportMap& vp,
                                         const BookTickerRowLike& row,
                                         qreal x0,
                                         qreal x1,
                                         bool includeEndSample) {
    constexpr int kSampleStepPx = 4;
    const int firstPx = std::max(0, static_cast<int>(std::round(x0)));
    int lastPx = std::min(static_cast<int>(std::round(vp.w)), static_cast<int>(std::round(x1)));
    if (!includeEndSample) {
        if (lastPx <= firstPx) return;
        --lastPx;
    }
    if (lastPx < firstPx) return;

    int xPx = firstPx;
    for (; xPx <= lastPx; xPx += kSampleStepPx) {
        trace.samples.push_back(BookTickerSample{xPx,
                                                 bookTickerTraceTimestampAtX(vp, xPx),
                                                 row.bidPriceE8,
                                                 row.bidQtyE8,
                                                 row.askPriceE8,
                                                 row.askQtyE8});
    }

    const int lastWritten = xPx - kSampleStepPx;
    if (lastWritten != lastPx) {
        trace.samples.push_back(BookTickerSample{lastPx,
                                                 bookTickerTraceTimestampAtX(vp, lastPx),
                                                 row.bidPriceE8,
                                                 row.bidQtyE8,
                                                 row.askPriceE8,
                                                 row.askQtyE8});
    }
}

template <typename BookTickerRowLike>
inline void appendBookTickerTraceSegment(BookTickerTrace& trace,
                                         BookTickerTraceBuildState& state,
                                         const ViewportMap& vp,
                                         const BookTickerRowLike& row,
                                         std::int64_t startTsNs,
                                         std::int64_t endTsNs,
                                         bool buildSamples,
                                         bool includeEndSample) {
    if (endTsNs < startTsNs || vp.w <= 0.0 || vp.h <= 0.0) return;

    const qreal x0 = bookTickerTraceRoundedX(vp, startTsNs);
    qreal x1 = bookTickerTraceRoundedX(vp, endTsNs);
    if (x1 <= x0 && x0 < static_cast<qreal>(vp.w)) x1 = x0 + 1.0;

    const bool bidVisible = bookTickerTraceVisiblePrice(vp, row.bidPriceE8);
    const bool askVisible = bookTickerTraceVisiblePrice(vp, row.askPriceE8);
    if (bidVisible) {
        appendBookTickerTraceLine(trace.bidLines, state.bid, x0, x1, bookTickerTraceRoundedY(vp, row.bidPriceE8));
    } else {
        state.bid.active = false;
    }
    if (askVisible) {
        appendBookTickerTraceLine(trace.askLines, state.ask, x0, x1, bookTickerTraceRoundedY(vp, row.askPriceE8));
    } else {
        state.ask.active = false;
    }
    if (buildSamples && (bidVisible || askVisible)) appendBookTickerTraceSamples(trace, vp, row, x0, x1, includeEndSample);
}

}  // namespace hftrec::gui::viewer::detail
