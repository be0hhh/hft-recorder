#pragma once

#include <QPointF>

namespace hftrec::gui::viewer {
struct RenderSnapshot;
struct HoverInfo;
}

namespace hftrec::gui::viewer::hit_test {

// Hit-tests `point` against the snapshot. Writes result into `out` without
// touching the snapshot — so the caller can keep the snapshot cached across
// hover events.
void computeHover(const RenderSnapshot& snap,
                  const QPointF& point,
                  bool contextActive,
                  HoverInfo& out);

}  // namespace hftrec::gui::viewer::hit_test
