#pragma once

namespace hftrec::gui::viewer {
struct RenderContext;
}

namespace hftrec::gui::viewer::renderers {

// CPU QPainter pass: walks RenderSnapshot::bookSegments and draws, per
// segment, a dim band between consecutive visible levels plus a crisp 1-px
// line per level. Pixel-honest — all Y coordinates are integer-snapped.
void renderBook(const RenderContext& ctx);

}  // namespace hftrec::gui::viewer::renderers
