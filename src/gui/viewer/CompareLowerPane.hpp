#pragma once

#include <QString>

#include "gui/viewer/StrategyIndicator.hpp"
#include "gui/viewer/StrategyOverlay.hpp"

namespace hftrec::gui::viewer {

enum class CompareLowerPaneKind {
    DefaultSpread,
    CandleSpread,
    MarketSpreadOverlay,
    StrategySpread,
    StrategyIndicator,
};

struct CompareLowerPaneState {
    CompareLowerPaneKind kind{CompareLowerPaneKind::DefaultSpread};
    bool hasData{false};
    QString title{};
};

QString compareLowerPaneKindId(CompareLowerPaneKind kind);

CompareLowerPaneState selectCompareLowerPane(const StrategyOverlayData& overlay,
                                             const StrategyIndicatorData& indicator,
                                             bool hasDefaultSpread,
                                             bool hasCandleSpread);

}  // namespace hftrec::gui::viewer
