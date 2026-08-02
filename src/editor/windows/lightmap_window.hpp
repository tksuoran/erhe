#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <unordered_map>
#include <vector>

namespace erhe::geometry { class Geometry; }
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

    // Leak camouflage (per_facet mode): re-unwrap with charts packed in
    // baked-luminance order so similarly lit facets are atlas neighbors and
    // cross-chart filter-tap / dilation pollution picks up similar values.
    // Needs a bake; the interactive baker restarts automatically after.
    // False when there is no bake or no lightmapped meshes. Also reachable
    // over MCP (lightmap_reorder_charts).
    auto reorder_charts_by_bake() -> bool;

    // Executes a reorder the window button requested. Called from the
    // editor tick BEFORE any lightmap commands are recorded: the reorder
    // reads the atlas back (standalone submit + wait idle + texture layout
    // transitions), which must not run in the middle of ImGui frame
    // construction while the open frame command buffer is using the same
    // texture - the button only sets a flag.
    void update();

private:
    // Queues an undoable Make_atlas_operation (usage_index 2, method knobs
    // from Lightmap_config) for every lightmapped, non-skinned mesh node in
    // the active scene. The optional per-facet chart order keys re-pack
    // similarly keyed facets next to each other (per_facet mode only).
    auto queue_generate_lightmap_uvs(std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>&& per_facet_chart_order) -> bool;
    void generate_lightmap_uvs();

    App_context& m_context;
    bool         m_reorder_requested{false};
};

} // namespace editor
