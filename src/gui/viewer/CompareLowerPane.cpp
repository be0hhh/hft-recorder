#include "gui/viewer/CompareLowerPane.hpp"

namespace hftrec::gui::viewer {

QString compareLowerPaneKindId(CompareLowerPaneKind kind) {
    switch (kind) {
        case CompareLowerPaneKind::StrategySpread:
            return QStringLiteral("strategy_spread");
        case CompareLowerPaneKind::StrategyIndicator:
            return QStringLiteral("strategy_indicator");
        case CompareLowerPaneKind::DefaultSpread:
        default:
            return QStringLiteral("default_spread");
    }
}

CompareLowerPaneState selectCompareLowerPane(const StrategyOverlayData& overlay,
                                             const StrategyIndicatorData& indicator,
                                             bool hasDefaultSpread) {
    if (!overlay.spreadPoints.empty()) {
        return CompareLowerPaneState{
            .kind = CompareLowerPaneKind::StrategySpread,
            .hasData = true,
            .title = QStringLiteral("Strategy spread"),
        };
    }
    if (!indicator.empty()) {
        QString title = indicator.title.isEmpty() ? indicator.profile : indicator.title;
        if (!indicator.auxLabel.isEmpty()) {
            title = title.isEmpty() ? indicator.auxLabel : QStringLiteral("%1 %2").arg(title, indicator.auxLabel);
        }
        return CompareLowerPaneState{
            .kind = CompareLowerPaneKind::StrategyIndicator,
            .hasData = true,
            .title = title.isEmpty() ? QStringLiteral("Strategy indicator") : title,
        };
    }
    return CompareLowerPaneState{
        .kind = CompareLowerPaneKind::DefaultSpread,
        .hasData = hasDefaultSpread,
        .title = QStringLiteral("BookTicker spread"),
    };
}

}  // namespace hftrec::gui::viewer
