#pragma once

#include "renderers/lightmap_grid.hpp"
#include "renderers/lightmap_partitioner.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <climits>
#include <memory>
#include <string>
#include <vector>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// Lightmap baking control window (doc/lightmap_baking_plan.md).
//
// Front door: Prepare World-Space Tiles (the partitioner). The window
// exposes the grid density knobs (cell size, tile texture size), the
// interactive/offline bakes, tile persistence and the per-tile
// subdivide/merge controls.
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

    // Leak camouflage (per_facet mode): re-prepare with the piece charts
    // packed in baked-luminance order so similarly lit facets are atlas
    // neighbors and cross-chart filter-tap / dilation pollution picks up
    // similar values. Needs a prepared partition and a bake. tile >= 0
    // reorders only that spatial tile's pieces (the others re-unwrap in
    // default order and restore from disk). False when there is no
    // partition, no bake, or a prepare is already in flight. Also
    // reachable over MCP (lightmap_reorder_charts).
    // NOTE: the bake data itself is NOT migrated to the new texel
    // locations yet - the atlas is stale until the next bake (see the
    // prompt queue item on reorder bake-data migration).
    auto reorder_charts_by_bake(int tile = -1) -> bool;

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
    // button's parameters); chart_order (optional) is the Reorder Charts
    // By Bake key set. False when nothing was launched.
    auto launch_prepare(std::shared_ptr<const Lightmap_partitioner::Params::Chart_order> chart_order = {}) -> bool;
    // Shared tail of subdivide/merge: sort + write the override list into
    // the scene settings and kick the relayout/re-prepare.
    void apply_tile_overrides(std::vector<Lightmap_tile_key>&& overrides);

    App_context& m_context;
    // Deferred Reorder Charts By Bake request (see update()): INT_MIN =
    // none, -1 = all tiles, >= 0 = that tile only.
    int          m_reorder_requested_tile{INT_MIN};
    // Auto-re-prepare debounce: editing a partitioned source (transform
    // move, geometry swap) marks the pieces stale; once the source state
    // hash stops changing for the debounce window, update() saves the
    // resident tiles (so restore-on-activate repopulates unaffected tiles
    // after the commit) and launches an async re-prepare. The old pieces +
    // old lightmap keep rendering until the commit swaps them.
    uint64_t     m_source_state_hash{0};
    int          m_source_stable_frames{0};
};

} // namespace editor
