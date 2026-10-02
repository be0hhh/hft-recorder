#pragma once

#include <QRectF>

#include <cstdint>

class QPainter;

namespace hftrec::gui::viewer {

struct RateLimitUsageData;

struct RateLimitGraphOptions {
    bool showLaneLabels{true};
    bool showLastValues{true};
    bool fillBackground{true};
};

void drawRateLimitGraph(QPainter& painter,
                        const QRectF& bounds,
                        const RateLimitUsageData& usage,
                        std::int64_t tsMin,
                        std::int64_t tsMax,
                        const RateLimitGraphOptions& options = {});

}  // namespace hftrec::gui::viewer
