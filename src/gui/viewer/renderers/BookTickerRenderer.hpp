#pragma once

namespace hftrec::gui::viewer {
struct RenderContext;
}

namespace hftrec::gui::viewer::renderers {

// Draws the prepared continuous bookTicker trace (bid & ask). 1-px opaque
// strokes using ColorScheme bid/ask colors.
void renderBookTicker(const RenderContext& ctx);

}  // namespace hftrec::gui::viewer::renderers
