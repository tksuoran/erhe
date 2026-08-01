#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// Lightmap baking control window (doc/lightmap_baking_plan.md).
//
// Phase 1: marks meshes as lightmapped (the Item flag is toggled in the
// Properties window), generates automatic lightmap UVs into texcoord
// channel 2 for every lightmapped mesh in the active scene, and exposes
// the texel-density setting. The Baking toggle arrives with the gather
// (plan phase 3); until then it is shown disabled.
class Lightmap_window : public erhe::imgui::Imgui_window
{
public:
    Lightmap_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    // Queues an undoable Make_atlas_operation (usage_index 2, ABF + xatlas,
    // hard angle from Lightmap_config) for every lightmapped, non-skinned
    // mesh node in the active scene.
    void generate_lightmap_uvs();

    App_context& m_context;
};

} // namespace editor
