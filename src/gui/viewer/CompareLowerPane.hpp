#pragma once

#include <QString>

#include "gui/viewer/StrategyIndicator.hpp"
#include "gui/viewer/StrategyOverlay.hpp"

namespace hftrec::gui::viewer {

enum class CompareLowerPaneKind {
    DefaultSpread,
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
                                             bool hasDefaultSpread);

}  // namespace hftrec::gui::viewer
