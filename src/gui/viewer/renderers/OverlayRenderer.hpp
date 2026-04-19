#pragma once

namespace hftrec::gui::viewer {
struct RenderContext;
}

namespace hftrec::gui::viewer::renderers {

// Tooltip card + halo + focus circle for the currently-hovered trade or
// book level. Reads from ctx.hov (hover is cached on the ChartItem side
// and copied into the render context — so overlay can re-render without
// invalidating the rest of the snapshot).
void renderOverlay(const RenderContext& ctx);

}  // namespace hftrec::gui::viewer::renderers
