#pragma once

namespace hftrec::gui::viewer {
struct RenderContext;
}

namespace hftrec::gui::viewer::renderers {

// Trade connector polyline (1-px, breaks on filtered trades) + AA circle
// dots sized by qty × price.
void renderTrades(const RenderContext& ctx);

}  // namespace hftrec::gui::viewer::renderers
