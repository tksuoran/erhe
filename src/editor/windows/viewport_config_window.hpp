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

    // Edits the per-scene-view Visual Style in place (render-style visibility
    // toggles only). Persistence is handled by the owning Scene_view's collect
    // callback (autosaved to editor_settings.json); this window does not save
    // directly. Editor-global visual style lives in the Settings window.
    // The context is needed by the Shadows mode combo: Baked Lightmaps
    // mirrors into the global Lightmap render-with-lightmaps state.
    static void imgui(App_context& context, Viewport_config& edit_data);

    // Sets the Visual Style shadow mode of EVERY scene view (viewports and
    // headset), then mirrors the global lightmap state. Used by the Lightmap
    // window's "Render with lightmaps" checkbox, which is a global control.
    static void set_shadow_mode_all_views(App_context& context, Shadow_mode mode);

    // Mirror per-view Visual Style -> global lightmap state: the partitioner's
    // render-with-lightmaps proxy swap (and its persisted Lightmap_config
    // flag) turns on when ANY scene view's shadow mode is Baked Lightmaps.
    // Call after changing any view's shadow_mode.
    static void sync_render_with_lightmaps(App_context& context);

    // Public API
    static void render_style_ui(Render_style_data& render_style);

};

}
