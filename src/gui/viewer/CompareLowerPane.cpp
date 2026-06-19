#include "gui/viewer/CompareLowerPane.hpp"

namespace hftrec::gui::viewer {

QString compareLowerPaneKindId(CompareLowerPaneKind kind) {
    switch (kind) {
        case CompareLowerPaneKind::StrategySpread:
            return QStringLiteral("strategy_spread");
        case CompareLowerPaneKind::StrategyIndicator:
            return QStringLiteral("strategy_indicator");
        case CompareLowerPaneKind::CandleSpread:
            return QStringLiteral("candle_spread");
        case CompareLowerPaneKind::MarketSpreadOverlay:
            return QStringLiteral("market_spread_overlay");
        case CompareLowerPaneKind::DefaultSpread:
        default:
            return QStringLiteral("default_spread");
    }
}

CompareLowerPaneState selectCompareLowerPane(const StrategyOverlayData& overlay,
                                             const StrategyIndicatorData& indicator,
                                             bool hasDefaultSpread,
                                             bool hasCandleSpread) {
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
    if (hasDefaultSpread && hasCandleSpread) {
        return CompareLowerPaneState{
            .kind = CompareLowerPaneKind::MarketSpreadOverlay,
            .hasData = true,
            .title = QStringLiteral("BookTicker + Candle spread"),
        };
    }
    if (hasCandleSpread) {
        return CompareLowerPaneState{
            .kind = CompareLowerPaneKind::CandleSpread,
            .hasData = true,
            .title = QStringLiteral("Candle close spread"),
        };
    }
    return CompareLowerPaneState{
        .kind = CompareLowerPaneKind::DefaultSpread,
        .hasData = hasDefaultSpread,
        .title = QStringLiteral("BookTicker spread"),
    };
}

}  // namespace hftrec::gui::viewer
