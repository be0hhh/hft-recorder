#pragma once

#include <QString>

#include "StrategyIndicator.hpp"
#include "StrategyOverlay.hpp"

namespace hftrec::gui::viewer {

enum class CompareLowerPaneKind {
    DefaultSpread,
    CandleSpread,
    MarketSpreadOverlay,
    StrategySpread,
    StrategyIndicator,
    RateLimitUsage,
};

struct CompareLowerPaneState {
    CompareLowerPaneKind kind{CompareLowerPaneKind::DefaultSpread};
    bool hasData{false};
    QString title{};
};

QString compareLowerPaneKindId(CompareLowerPaneKind kind);

CompareLowerPaneState selectCompareLowerPane(const StrategyOverlayData& overlay,
                                             const StrategyIndicatorData& indicator,
                                             bool preferRateLimitUsage,
                                             bool hasRateLimitUsage,
                                             bool hasDefaultSpread,
                                             bool hasCandleSpread);

}  // namespace hftrec::gui::viewer
