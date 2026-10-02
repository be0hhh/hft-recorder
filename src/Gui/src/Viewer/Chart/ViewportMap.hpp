#pragma once

#include <cstdint>

namespace hftrec::gui::viewer {

// Coordinate transform for the chart viewport. (tsNs, priceE8) -> (x, y) px.
// Promoted from ChartItem.cpp anonymous namespace so renderers + hit-test can
// share the exact same math.
struct ViewportMap {
    std::int64_t tMin{0};
    std::int64_t tMax{1};
    std::int64_t pMin{0};
    std::int64_t pMax{1};
    double w{0.0};
    double h{0.0};

    double toX(std::int64_t ts) const noexcept {
        const double span = static_cast<double>(tMax - tMin);
        if (span <= 0.0) return 0.0;
        return static_cast<double>(ts - tMin) * w / span;
    }

    double toY(std::int64_t price) const noexcept {
        const double span = static_cast<double>(pMax - pMin);
        if (span <= 0.0) return 0.0;
        return h - static_cast<double>(price - pMin) * h / span;
    }
};

}  // namespace hftrec::gui::viewer
