#pragma once

#include "gui/viewer/RenderSnapshot.hpp"

class QPainter;

namespace hftrec::gui::viewer {

// Thin bundle passed to every renderer. Keeps renderer signatures uniform.
// `s` is a const ref to the cached per-viewport geometry (big vectors —
// never copy). `hov` is rebuilt cheap on every paint from the item's cached
// hit-test result, so overlays always see the latest hover without forcing
// a snapshot rebuild.
struct RenderContext {
    QPainter*             p;
    const RenderSnapshot& s;
    HoverInfo             hov{};
    double                dpr{1.0};
};

}  // namespace hftrec::gui::viewer
