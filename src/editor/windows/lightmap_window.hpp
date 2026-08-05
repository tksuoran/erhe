#pragma once

#include "renderers/lightmap_grid.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <string>
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

    // Arms the baker's offline batch bake (one tile per frame from
    // update()): payloads + manifest land in <scene>.lightmap/ via
    // Lightmap_tile_io. False when there is no layout / baker. Also
    // reachable over MCP (lightmap_bake_to_disk). UI: "Batch Process All
    // Tiles".
    auto start_bake_to_disk() -> bool;

    // Persists one resident, published tile's current lightmap (display
    // slot readback -> tile_<id>.lmt + manifest). Used by the save-on-evict
    // drain in update() and the Save All Tiles button.
    auto save_tile_to_disk(int tile) -> bool;

    // Saves every resident, published tile. Returns how many were saved.
    auto save_all_tiles() -> std::size_t;

    // Quadtree grid density control (scene-persisted leaf overrides,
    // Scene_settings::lightmap_tile_overrides). subdivide_tile splits a
    // current leaf into 4 half-size cells (2x nominal texel density);
    // merge_tile merges the given leaf and its 3 siblings into their
    // parent (half density). Both validate against the current override
    // set, write the scene settings, and - with a live partition - launch
    // an async re-prepare (the legacy path relayouts through the tick's
    // grid hash). Returns an empty string on success, else the reason.
    // Also reachable over MCP (lightmap_subdivide_tile /
    // lightmap_merge_tile).
    auto subdivide_tile(const Lightmap_tile_key& key) -> std::string;
    auto merge_tile    (const Lightmap_tile_key& key) -> std::string;

private:
    // Launch an async re-prepare with the current config (the Prepare
    // button's parameters). False when nothing was launched.
    auto launch_prepare() -> bool;
    // Shared tail of subdivide/merge: sort + write the override list into
    // the scene settings and kick the relayout/re-prepare.
    void apply_tile_overrides(std::vector<Lightmap_tile_key>&& overrides);

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
