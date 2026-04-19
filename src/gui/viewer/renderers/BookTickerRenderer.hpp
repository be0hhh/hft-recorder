#pragma once

namespace hftrec::gui::viewer {
struct RenderContext;
}

namespace hftrec::gui::viewer::renderers {

// Draws two step paths (bid & ask) across visible bookSegments. 1-px
// opaque strokes using ColorScheme bid/ask colors.
void renderBookTicker(const RenderContext& ctx);

}  // namespace hftrec::gui::viewer::renderers
