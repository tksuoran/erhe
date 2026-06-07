#pragma once

#include "renderers/viewport_config.hpp"

#include <glm/glm.hpp>

namespace editor {

class App_context;

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

    static void imgui(Viewport_config& edit_data);

    // Public API
    static void render_style_ui(Render_style_data& render_style, bool& edited);

};

}
