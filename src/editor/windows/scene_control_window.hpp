#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <functional>

struct Graphics_preset_entry;

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// A session-only change of the graphics preset in effect (as MCP
// set_graphics_preset: graphics_presets.json is not written), queued out of
// the ImGui iteration.
void queue_session_preset_edit(App_context& context, std::function<void(Graphics_preset_entry&)> edit);

// "Headlamp only" lighting: per type loops with one spot light slot (shadow
// mapped when the headlamp casts shadows) and no other light.
void set_headlamp_only_lighting(Graphics_preset_entry& preset, bool cast_shadow);
[[nodiscard]] auto is_headlamp_only_lighting(const Graphics_preset_entry& preset) -> bool;

// The Scene Control window (doc/editor/tools.md "Scene Control window"):
// session lighting and LOD controls everywhere, and under OpenXR the
// headset's own controls (Headset_view::scene_control_imgui()).
class Scene_control_window : public erhe::imgui::Imgui_window
{
public:
    Scene_control_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void lighting_and_lod_imgui();
    void culling_imgui();
    void hud_placement_imgui();

    App_context& m_context;
};

} // namespace editor
