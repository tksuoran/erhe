#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace editor {

// Common base for the node-graph editor windows (the geometry graph and the
// texture graph). Today it only carries the seam the companion palette window
// forwards to; later Phase C steps grow it into the shared canvas / link /
// palette / target machinery so the concrete windows keep only their
// payload-specific hooks.
class Graph_editor_window_base : public erhe::imgui::Imgui_window
{
public:
    using erhe::imgui::Imgui_window::Imgui_window;

    // Draws the node palette (search box + categories). The companion
    // Graph_editor_palette_window forwards here so the palette and the canvas
    // can be docked / sized independently.
    virtual void controls_imgui() = 0;
};

} // namespace editor
