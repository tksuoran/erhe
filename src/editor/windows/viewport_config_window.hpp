#pragma once

#include "renderers/viewport_config.hpp"

#include <glm/glm.hpp>

namespace editor {

class Viewport_config_window
{
public:
    // Indexed by Visualization_mode (off, selected, hovered, all).
    static constexpr const char* c_visualization_mode_strings[] = {
        "Off",
        "Selected",
        "Hovered",
        "All"
    };

    // Edits the per-scene-view Visual Style in place (render-style visibility
    // toggles only). Persistence is handled by the owning Scene_view's collect
    // callback (autosaved to editor_settings.json); this window does not save
    // directly. Editor-global visual style lives in the Settings window.
    static void imgui(Viewport_config& edit_data);

    // Public API
    static void render_style_ui(Render_style_data& render_style);

};

}
