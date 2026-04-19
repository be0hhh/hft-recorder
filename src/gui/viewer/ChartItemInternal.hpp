#pragma once

#include "gui/viewer/ChartItem.hpp"

namespace hftrec::gui::viewer::detail {

SnapshotInputs collectInputs(const ChartItem& item);
HoverInfo buildHoverInfo(const ChartItem& item);

}  // namespace hftrec::gui::viewer::detail
