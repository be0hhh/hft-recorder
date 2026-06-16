#pragma once

#include "gui/viewer/ChartItem.hpp"

namespace hftrec::gui::viewer::detail {

SnapshotInputs collectInputs(const ChartItem& item);
HoverInfo buildHoverInfo(const ChartItem& item);

inline bool shouldRenderStrategyOverlayInFinalPass(const RenderSnapshot& snap, bool interactiveMode) noexcept {
    return !interactiveMode
        && (!snap.strategyOrderSegments.empty()
            || !snap.strategyFillMarkers.empty()
            || !snap.strategyRangePoints.empty());
}

}  // namespace hftrec::gui::viewer::detail
