#pragma once

#include "renderers/lightmap_grid.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/operation/clip_tile_tree.hpp"
#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <span>
#include <vector>

#include <array>

namespace erhe::graphics {
    class Acceleration_structure;
    class Acceleration_structure_instance;
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_command_encoder;
    class Compute_pipeline;
    class Device;
    class Fragment_outputs;
    class Render_pipeline;
    class Ring_buffer_client;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
    class Vertex_input_state;
}
namespace erhe::geometry { class Geometry; }
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::scene { class Mesh; }
namespace erhe::scene_renderer { class Mesh_memory; }

namespace editor {

class Lightmap_partitioner;
class Lightmap_report;
class Scene_root;

// Lightmap baker (doc/lightmap_baking_plan.md).
//
// Phase 2: the per-instance atlas layout (milestone A) and the texel
// G-buffer raster pass (milestone B). Each lightmapped, non-skinned
// content mesh primitive with channel-2 UVs gets a rectangle in a single
// square atlas page, sized by world-space surface area times texel
// density. The G-buffer pass then rasterizes every region's triangles in
// atlas UV space, storing world position (RGBA32F, w = coverage) and
// world normal (RGBA16F) per texel - the input the ray-query gather (plan
// phase 3) consumes.
//
// UI-free by design (plan section 6) - the Lightmap window and MCP tools
// are thin clients.
class Lightmap_baker
{
public:
    // One packed mesh primitive. mesh+primitive_index identify the source;
    // the rect is the content region in TILE-LOCAL texels (padding lives
    // outside it), uv_scale_offset maps chart UV into TILE UV. The display-
    // atlas mapping depends on the tile's resident slot and is derived via
    // Atlas_layout::display_uv_scale_offset.
    class Instance_region
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        primitive_index{0};
        glm::vec4                          uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        int                                x{0};
        int                                y{0};
        int                                width{0};
        int                                height{0};
        // Spatial tile (Atlas_layout::tiles index) the region packs into;
        // regions never span tiles, so the bake working set (G-buffer,
        // working atlas, accumulation) only ever needs one tile's worth of
        // texels at a time.
        int                                tile{0};
        float                              world_area{0.0f}; // m^2
        // Fraction of [0,1]^2 chart space the primitive's facets actually
        // cover (gutters / packing waste / min-chart upscales excluded).
        // Region sizing divides by it so texels-per-meter holds exactly
        // per facet instead of being diluted by packing efficiency.
        float                              uv_coverage{1.0f};
        // Smallest facet UV AABB extent (shorter axis, normalized atlas
        // UV). Region sizing raises the side so this facet still spans
        // min_face_texels: the unwrap's own min-size clamp cannot deliver
        // it when MOST facets are tiny (scaling every chart up grows
        // coverage and shrinks the region right back - texels per facet
        // are invariant to it), so the region itself must grow.
        float                              min_facet_uv_extent{1.0f};
        // Partitioned mode (world-space tile pieces): the primitive index of
        // the ORIGINAL mesh this piece came from and the piece ordinal
        // within that (source mesh, source primitive) - together with the
        // node path they form the on-disk region identity. piece_ordinal is
        // -1 for ordinary (non-piece) regions.
        std::size_t                        source_primitive_index{0};
        int                                piece_ordinal{-1};
    };

    // One spatial tile of the adaptive world-space (XZ) partition. Every
    // tile packs its member regions into one tile_size^2 texel square;
    // tiles are unbounded in count, so any world extent lays out with
    // bounded per-tile memory. A tile becomes visible by holding a slot in
    // the display atlas (a grid of get_slot_count() tile-sized sub-rects);
    // non-resident tiles keep their layout but render unlit.
    class Tile
    {
    public:
        // Quadtree grid identity (world-origin-anchored; stable across
        // sessions and content edits) and the cell's world XZ box.
        Lightmap_tile_key key{};
        erhe::math::Aabb cell_bounds {};       // the grid cell (XZ exact; Y = content extent)
        erhe::math::Aabb world_bounds{};       // union of member instance world AABBs (camera ranking)
        // Nominal texel density of this tile: tile_texture_size / cell side.
        float            texels_per_meter{0.0f};
        // < 1 when the tile's regions had to shrink to fit tile_size (the
        // down-only density flex for content denser than the tile texture
        // allows); effective texel density = texels_per_meter * this.
        float            density_scale{1.0f};
        int              slot{-1};             // display atlas slot; -1 = not resident
        bool             has_content{false};   // at least one region packed into this tile
    };

    class Atlas_layout
    {
    public:
        // Display atlas size: slots_x/slots_y tile-sized slots per axis.
        int                          width {0};
        int                          height{0};
        int                          slots_x{1};
        int                          slots_y{1};
        // Texel side of every tile (and so of every display slot).
        int                          tile_size{0};
        std::vector<Tile>            tiles;
        std::vector<Instance_region> regions;
        // The kd split tree behind `tiles`, with explicit world-space plane
        // values (node 0 = root; leaf nodes carry the tile index). This is
        // the partition the world-space mesh clipper
        // (erhe::geometry::operation::clip_by_tile_tree) cuts against, so
        // clipped pieces land in exactly the tiles the packer assigned.
        // Overflow splits (co-located regions divided by count, and pack-
        // failure re-splits of co-located sets) carry axis == -1: they have
        // no spatial plane, and their tiles overlap in world space.
        std::vector<erhe::geometry::operation::Clip_tree_node> kd_nodes;
        // True when the regions are world-space tile pieces from the
        // Lightmap_partitioner. Non-resident regions then publish the
        // white-fallback sentinel vec4(-1,0,0,0) instead of the vec4(0)
        // no-lightmap gate (see display_uv_scale_offset).
        bool partitioned{false};

        [[nodiscard]] auto get_tile_count () const -> int { return static_cast<int>(tiles.size()); }
        [[nodiscard]] auto get_slot_count () const -> int { return slots_x * slots_y; }
        [[nodiscard]] auto get_slot_origin(int slot) const -> glm::ivec2;
        [[nodiscard]] auto get_tile_size  () const -> int { return tile_size; }
        // Chart UV -> display atlas UV for the region's tile's current
        // slot; zero (the renderer's "no lightmap" gate) when the tile is
        // not resident.
        [[nodiscard]] auto display_uv_scale_offset(const Instance_region& region) const -> glm::vec4;
    };

    // Optional bake features (Lightmap window checkboxes; viewport bicubic
    // sampling is not here - it lives in the forward renderer). set_options()
    // reacts to changes: terminator_fix re-rasters the G-buffer, indirect
    // bounce restarts accumulation, publish-stage toggles (denoise, dilation,
    // seam blend) force a republish on the next tick.
    // G-buffer texel coverage strategy (article alignment item 1 - how a
    // texel whose center misses every triangle still gets a sample).
    enum class Coverage_mode : int {
        conservative = 0, // native conservative rasterization; falls back to jitter_9 without the extension
        jitter_9     = 1, // 9-tap sub-texel jitter re-render (Bakery-style)
        jitter_25    = 2  // 25-tap sub-texel jitter re-render (denser edge coverage)
    };

    class Bake_options
    {
    public:
        bool  indirect_bounce{true};
        bool  terminator_fix {true};
        bool  denoise        {true};
        bool  dilation       {true};
        bool  seam_blend     {true};
        Coverage_mode coverage_mode{Coverage_mode::conservative};
        // Frostbite Flux texel supersampling (slide "Texel sampling (2)"):
        // sample positions rasterized on a regular sub-texel grid; every
        // shadow/bounce ray starts from a uniform-randomly picked valid
        // point of its texel instead of the one fixed per-texel origin.
        // Grid side per texel: 0 = off, 4 = 16 points, 8 = 64 points (the
        // Flux default).
        int   supersample_factor{0};
        // Chart gutter width (texels at the expected density; the unwrap's
        // uv_gutter_texels). Dilation is clamped to half of it so a chart
        // never fills gutter texels the neighboring chart's sampling
        // footprint owns.
        float gutter_texels  {3.0f};
    };

    // Procedural sky lighting for the gather's hemisphere ray (plan: "Next
    // up - procedural sky lighting"). LUTs belong to Sky_renderer; the
    // caller refreshes this every frame before tick() (the LUT pointers
    // must stay valid across the frame). Sky contributes only when enabled
    // AND both LUTs are set; a change resets accumulation via the lighting
    // hash.
    class Sky_lighting
    {
    public:
        erhe::graphics::Texture* transmittance_lut{nullptr};
        erhe::graphics::Texture* multiscatter_lut {nullptr};
        // xyz = toward-sun direction (world), w = sun illuminance.
        glm::vec4                sun_direction_and_intensity{0.0f, 1.0f, 0.0f, 0.0f};
        // x = atmosphere march steps, y = observer altitude (km).
        glm::vec4                sky_params{32.0f, 0.5f, 0.0f, 0.0f};
        bool                     enabled{false};
    };

    Lightmap_baker(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory);
    ~Lightmap_baker() noexcept;

    void set_options(const Bake_options& options);
    [[nodiscard]] auto get_options() const -> const Bake_options& { return m_options; }

    void set_sky_lighting(const Sky_lighting& sky) { m_sky = sky; }

    // Tile texture side (power of two, default 1024) and how many tiles may
    // be resident in the display atlas at once (default 9 = a 3x3 slot
    // grid). Changing either takes effect on the next update_layout().
    void set_tile_config(int tile_texture_size, int resident_tile_budget);

    // World-space side of one grid cell at quadtree level 0 (meters); the
    // uniform grid is anchored at the world origin. Together with the tile
    // texture size this sets each tile's nominal texel density. Takes
    // effect on the next update_layout() / prepare.
    void set_cell_size(float cell_size_m);
    [[nodiscard]] auto get_cell_size() const -> float { return m_cell_size; }

    // Quadtree leaf overrides (Scene_settings::lightmap_tile_overrides):
    // each {level, ix, iz} with level != 0 replaces the level-0 cells it
    // covers. Pushed by the editor tick from the active scene; consumed by
    // the next layout / split estimate.
    void set_tile_overrides(const std::vector<glm::ivec3>& overrides);
    [[nodiscard]] auto get_tile_overrides() const -> const std::vector<glm::ivec3>& { return m_tile_overrides; }
    // FNV hash of cell size + overrides, mixed into the tick's layout hash
    // so grid changes relayout (and, with a live partition, re-prepare).
    [[nodiscard]] auto get_grid_parameters_hash() const -> uint64_t;

    // Failure/warning sink (layout density flex, overflow tiles, budget
    // clamps); optional, shown by the Lightmap window.
    void set_report(Lightmap_report* report) { m_report = report; }

    // World-space tile partitioner (optional). When it has a prepared
    // partition for the layout scene, update_layout() derives its regions
    // from the piece meshes with their pre-assigned tiles (no kd re-split;
    // the tile tree comes from the stored partition).
    void set_partitioner(const Lightmap_partitioner* partitioner) { m_partitioner = partitioner; }

    // Tile-bounds debug visualization (Lightmap window toggle, drawn by
    // Debug_visualizations): x-ray wireframe world AABBs of every spatial
    // tile - purple for all, white for the active (slot-holding) ones.
    void set_show_tile_bounds(bool show)                { m_show_tile_bounds = show; }
    [[nodiscard]] auto get_show_tile_bounds() const -> bool { return m_show_tile_bounds; }

    [[nodiscard]] auto is_supported() const -> bool;

    // True when the ray-query gather machinery exists - false without
    // Device_info::use_ray_query. Layout and G-buffer (is_supported) still
    // work then; only the bakes are unavailable.
    [[nodiscard]] auto is_bake_supported() const -> bool;

    // Recompute the layout from the prepared world-space partition for this
    // scene: piece meshes with pre-assigned grid tiles, each tile packing
    // its regions into one tile_size^2 texel square at the tile's nominal
    // density (tile_texture_size / cell side; flexing down per tile only
    // when content does not fit). Returns false (and clears the layout)
    // when no partition is prepared for the scene - Prepare World-Space
    // Tiles is the only front door. min_face_texels > 0 additionally grows
    // each region (capped at 4x the density-derived side) so its smallest
    // facet spans at least that many texels on its shorter UV axis.
    auto update_layout(Scene_root& scene_root, float min_face_texels) -> bool;

    // One source primitive of the estimate split with its assigned spatial
    // tile - all the partitioner needs to clip against the returned tree.
    class Estimate_region
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        primitive_index{0};
        int                                tile{-1};
    };
    class Estimate_split
    {
    public:
        std::vector<erhe::geometry::operation::Clip_tree_node> kd_nodes;
        std::vector<Estimate_region>                           regions;
        // Grid tile metadata (key, cell bounds, nominal density), index-
        // aligned with the kd leaves' tile indices; the partitioner stores
        // it for per-piece unwrap density and the partitioned relayout.
        std::vector<Tile>                                      tiles;
        int                                                    tile_count{0};
        [[nodiscard]] auto empty() const -> bool { return regions.empty() || kd_nodes.empty(); }
    };

    // Geometry-only spatial split for the world-space partitioner: same
    // enumeration as update_layout MINUS the channel-2 UV requirement,
    // sizing each region from its world area with an assumed nominal chart
    // coverage. The boundaries are an estimate - the partitioner clips
    // against the returned tree, unwraps each piece, and the partitioned
    // relayout re-packs with the pieces' MEASURED UVs - so no unwrap is
    // needed beforehand. Does NOT touch the active layout (m_layout).
    auto compute_tile_split_estimate(Scene_root& scene_root) -> Estimate_split;

    // Push every region's display (slot-space) uv_scale_offset onto its
    // Mesh_primitive (zero / white-fallback sentinel for non-resident
    // tiles). Ticks do this on residency changes; the partitioner calls it
    // after a partitioned relayout so piece meshes get their mappings
    // without waiting for a bake tick.
    void publish_regions();

    // Immediately clear the display atlas to white (standalone submit).
    // The partitioner calls this right after the commit relayout: the
    // fresh mappings must not sample the previous bake's texels at the
    // previous packing (rubbish); white is the unbaked-piece look. No-op
    // before the display atlas exists (it is created cleared white).
    void clear_display_to_white();

    // ---- Interactive-bake persistence (Lightmap_window drives it) ----
    // With save-on-evict enabled, the residency swap never drops a tile
    // whose published content has not been saved: it parks the tile id in
    // a pending-save queue and keeps its display slot (gathering stops).
    // Lightmap_window::update() drains the queue at a safe point in the
    // frame - readback + tile_<id>.lmt + manifest write - then calls
    // mark_tile_saved(), letting the next tick complete the eviction.
    void set_save_on_evict(bool enabled) { m_save_on_evict = enabled; }
    [[nodiscard]] auto take_tile_pending_save() -> int; // -1 = none pending
    void mark_tile_saved(int tile);
    // Resident + published + newer than the last save.
    [[nodiscard]] auto tile_has_unsaved_content(int tile) const -> bool;
    // Reads back a resident tile's published lightmap (its display-atlas
    // slot sub-rect) as tightly packed RGBA16F rows.
    auto read_back_tile(int tile, std::vector<uint16_t>& out_rgba16) -> bool;
    // Restore-on-activate (the inverse of save-on-evict): a tile gaining a
    // display slot with no accumulated content is queued here; the owner
    // (Lightmap_window::update) validates the saved payload against the
    // current bake parameters and layout, then hands the pixels back via
    // restore_tile() so a previously baked tile reappears instantly instead
    // of visibly re-baking from black. Accumulation still restarts (the
    // fp32 accum is not persisted), but republish is held until it reaches
    // saved_sweeps, so the display never regresses.
    [[nodiscard]] auto take_tile_pending_restore() -> int; // -1 = none pending
    auto restore_tile(int tile, int width, int height, std::span<const uint16_t> rgba16, uint32_t saved_sweeps) -> bool;
    // Completed accumulation sweeps behind the tile's published content
    // (persisted into the payload header by the save paths).
    [[nodiscard]] auto get_tile_sweeps(int tile) const -> uint32_t;
    // The tile gathers: it has content and is within the camera clamp /
    // resident set (Tile_state::active). Drives the per-active-set reorder.
    [[nodiscard]] auto is_tile_active(int tile) const -> bool;

    [[nodiscard]] auto get_layout() const -> const Atlas_layout& { return m_layout; }

    // Rasterize the texel G-buffer for one spatial tile of the current
    // layout: one draw per region of the tile, positions mapped through
    // channel-2 UVs into the region's tile-local rect. The G-buffer
    // targets are tile-sized and hold exactly one tile at a time
    // (get_gbuffer_tile). Standalone submit (own command buffer + wait
    // idle). Returns false when there is no layout or the pipeline is
    // unavailable.
    auto bake_gbuffer(int tile = 0) -> bool;
    [[nodiscard]] auto get_gbuffer_tile() const -> int { return m_gbuffer_tile; }

    // Debug: write the current tile's G-buffer as 8-bit PNGs
    // (<base>_position.png with position mapped into the covered world
    // bounds, <base>_normal.png as normal * 0.5 + 0.5; alpha = coverage).
    // Requires bake_gbuffer().
    auto debug_write_gbuffer_pngs(const std::string& base_path) -> bool;

    // Direct lighting gather (plan phase 3, first milestone): per valid
    // G-buffer texel, explicit sampling of every scene light with a
    // ray-query shadow ray against a BLAS/TLAS of ALL non-skinned content
    // meshes (occluders are not limited to lightmapped meshes). Writes
    // irradiance into the lightmap atlas texture. Standalone submit like
    // bake_gbuffer(); requires bake_gbuffer() first.
    auto bake_direct(Scene_root& scene_root) -> bool;

    // Debug: tone-mapped 8-bit PNG of the baked lightmap atlas.
    auto debug_write_lightmap_png(const std::string& path) -> bool;

    // CPU readback of the published atlas (RGBA32F, layout page size).
    // Standalone submit + wait idle; false when no bake exists.
    auto read_lightmap(std::vector<float>& out_rgba) -> bool;

    // Similar-color chart adjacency (leak camouflage): per-facet baked
    // luminance for every lightmapped geometry with a region, sampled at
    // each facet's chart UV centroid from the published atlas. Feed the
    // result to the partitioner's per-facet chart order (via the Lightmap
    // window's Reorder Charts By Bake) so a re-unwrap packs similarly lit
    // facets next to each other - cross-chart filter-tap / dilation
    // pollution then picks up similar values. Empty when no bake exists.
    // Keyed by the CURRENT region geometry (the piece geometry in
    // partitioned mode). tile >= 0 restricts the keys to that spatial
    // tile's regions (per-tile reorder).
    auto build_chart_order_keys(int tile = -1) -> std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>;

    // Interactive bake loop (plan section 3a): record one budgeted gather
    // slice plus resolve + dilate publish into the given command buffer
    // (the open frame command buffer; the rendergraph samples the published
    // atlas later in the same submission). Handles change-driven
    // invalidation internally: light/occluder edits reset accumulation,
    // lightmapped-mesh edits additionally re-raster the G-buffer.
    //
    // The gather walks the layout one spatial tile at a time (the working
    // set is tile-sized); the N tiles whose world bounds are nearest the
    // camera hold the display slots and gather (N = the resident budget,
    // further clamped by max_active_tiles > 0 when a camera position is
    // given). Tiles that lose their slot render unlit and release their
    // accumulation textures.
    void tick(
        erhe::graphics::Command_buffer& command_buffer,
        Scene_root&                     scene_root,
        float                           min_face_texels,
        const glm::vec3*                camera_position  = nullptr,
        int                             max_active_tiles = 0
    );

    // ---- Offline bake-to-disk (one tile at a time, bounded memory) ----
    //
    // start_offline_bake arms the state machine; offline_tick() (call once
    // per frame while is_offline_bake_active()) bakes ONE tile per call:
    // G-buffer raster, target_sweeps full-tile gather submits, resolve +
    // dilate + seam blend, CPU readback of the tile-sized working atlas,
    // then hands the fp16 pixels to the sink (which persists them, e.g.
    // via Lightmap_tile_io) and releases the tile's accumulation. Only one
    // tile's working set is ever resident, so any world size bakes within
    // the fixed scratch budget. Standalone submits only - no frame command
    // buffer needed - but each call blocks for that tile's full bake.
    //
    // The sink returns false to abort the bake (e.g. disk write failed).
    using Offline_tile_sink = std::function<bool(
        int                          tile,
        int                          width,
        int                          height,
        const std::vector<uint16_t>& rgba16 // packed fp16 RGBA rows
    )>;
    class Offline_progress
    {
    public:
        bool     active       {false};
        int      tiles_done   {0};
        int      tile_count   {0};
        uint32_t target_sweeps{0};
    };
    auto start_offline_bake(Scene_root& scene_root, uint32_t target_sweeps, Offline_tile_sink sink) -> bool;
    void cancel_offline_bake();
    [[nodiscard]] auto is_offline_bake_active() const -> bool { return m_offline_state.progress.active; }
    [[nodiscard]] auto get_offline_progress  () const -> const Offline_progress& { return m_offline_state.progress; }
    // Bake ONE pending tile; returns false when the bake just finished or
    // aborted (check the report for errors).
    auto offline_tick(Scene_root& scene_root) -> bool;

    // FNV hash of the parameters that invalidate persisted tiles: tile
    // size, cell size and the bake feature set. Stored in the manifest
    // and payload headers for stale-bake detection.
    [[nodiscard]] auto get_bake_parameters_hash() const -> uint64_t;

    // Stop = PAUSE: disabling stops gathering but keeps the whole bake
    // working set (accumulation, sweep counts, G-buffer, BLAS/TLAS) so
    // re-enabling continues exactly where it paused - request_reset() is
    // the explicit restart. Pausing also queues every resident, published,
    // unsaved tile for autosave (the save-on-evict drain persists them),
    // so the disk set matches the paused display and the streamer can take
    // the lightmap binding over seamlessly. The display atlas keeps
    // showing the last publish throughout.
    void set_baking_enabled(bool enabled);
    [[nodiscard]] auto is_baking_enabled() const -> bool     { return m_baking_enabled; }
    // Single Iteration: run the bake until every active content tile
    // reaches at least the current minimum sweep count + 1, then pause
    // (with the pause autosave). Tiles ahead of the minimum wait for the
    // others, so repeated requests advance all tiles in lockstep. While
    // already baking this turns into "finish the current sweep, then
    // pause".
    void request_single_iteration();
    // The display atlas holds at least one published tile of the current
    // layout (UI "Paused" state).
    [[nodiscard]] auto has_published_display() const -> bool;
    // ... and at least one of those tiles is newer than its last save. The
    // paused baker keeps the lightmap binding exactly while this is true:
    // handing the binding to the disk streamer would drop the in-progress
    // bake from the viewport. Once everything published is on disk the
    // streamer can take over seamlessly (same pixels) and camera-driven
    // tile streaming resumes.
    [[nodiscard]] auto has_unsaved_published_display() const -> bool;
    void request_reset() { m_reset_requested = true; }
    // Completed accumulation sweeps since the last reset: the minimum over
    // the active tiles' per-tile sweep counts, so every ACTIVE valid texel
    // holds at least this many samples.
    [[nodiscard]] auto get_sweep_count() const -> uint32_t;
    [[nodiscard]] auto get_cursor_row () const -> int        { return m_cursor_y; }
    [[nodiscard]] auto get_cursor_tile() const -> int        { return m_cursor_tile; }

    // Display atlas the forward renderer samples: a copy of the working
    // atlas taken only at publish points (complete sweeps), so bake resets
    // (transform / light edits) keep showing the previous result instead of
    // blacking out the scene while accumulation restarts.
    [[nodiscard]] auto get_lightmap_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_display_texture; }

    [[nodiscard]] auto get_position_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_position_texture; }
    [[nodiscard]] auto get_normal_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_normal_texture; }
    [[nodiscard]] auto get_albedo_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_albedo_texture; }

    // Default tile texture side (set_tile_config overrides; power of two,
    // clamped to [s_min_tile, s_max_tile]). Regions never span a tile, and
    // the bake scratch targets (G-buffer x4, working atlas, dilate scratch,
    // supersample origins) are tile-sized - the gather visits one tile at a
    // time - so scratch cost is bounded at ~60 bytes x tile_size^2 (~250 MB
    // at 2048) regardless of world extents. Per-tile fp32 accumulation
    // textures allocate lazily and only resident (slot-holding) tiles keep
    // one.
    static constexpr int s_tile     = 2048;
    static constexpr int s_min_tile = 256;
    static constexpr int s_max_tile = 8192;
    // Default resident-tile budget: display slots (and so tiles baked /
    // shown at once); 9 = a 3x3 slot grid around the camera.
    static constexpr int s_default_slot_budget = 9;
    static constexpr int s_padding  = 4; // texels around each region (mips + bilinear)

private:
    void ensure_gbuffer_targets();
    void ensure_bake_targets(erhe::graphics::Command_buffer& command_buffer);
    // Lazily create (and clear when dirty) the given tile's accumulation
    // texture; returns it. Records the clear into command_buffer.
    auto ensure_tile_accum(erhe::graphics::Command_buffer& command_buffer, int tile) -> erhe::graphics::Texture*;
    // Free every rebuildable working-set allocation, keeping only the
    // display atlas (see set_baking_enabled).
    void release_working_set();
    // Pause autosave: park resident published unsaved tiles in the
    // pending-save queue for the owner's drain.
    void queue_dirty_tiles_for_save();

    // Per-TLAS-instance record for the gather's bounce-ray attribute fetch
    // (mirrors Ray_trace_renderer::Instance_record_data; std430). Instances
    // without a lightmap region carry uv_scale_offset.x == 0 and bounce
    // rays hitting them contribute nothing.
    class Lm_instance_record
    {
    public:
        uint64_t  index_address        {0}; // first triangle index of the instance
        uint64_t  vertex_address       {0}; // start of the stream-1 vertex range
        uint64_t  position_address     {0}; // start of the stream-0 vertex range (position-fetch fallback)
        uint32_t  vertex_stride_uints  {0};
        uint32_t  position_stride_uints{0};
        glm::vec4 uv_scale_offset      {0.0f, 0.0f, 0.0f, 0.0f};
    };

    class Light_record
    {
    public:
        glm::vec4 position_and_type;
        glm::vec4 direction_and_outer_cos;
        glm::vec4 radiance_and_range;
        glm::vec4 params;
    };

    [[nodiscard]] auto collect_lights(Scene_root& scene_root) const -> std::vector<Light_record>;
    void write_gather_ubo(std::byte* data, const std::vector<Light_record>& lights, uint32_t frame_index, uint32_t base_texel_y) const;
    // Collect occluder instances + gather records; builds missing BLAS into
    // the command buffer. Every visible non-skinned content mesh occludes.
    void collect_instances(
        erhe::graphics::Command_buffer&                              command_buffer,
        Scene_root&                                                  scene_root,
        std::vector<erhe::graphics::Acceleration_structure_instance>& out_instances,
        std::vector<Lm_instance_record>&                             out_records
    );
    // Record resolve (tile accum -> working running average), optionally
    // the JNLM denoise, then the dilation ping-pong; leaves the working
    // atlas shader-readable. Operates on the tile-sized targets; the
    // G-buffer must hold the same tile (denoise guides).
    void record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer, int tile, bool with_denoise);
    // Copy the tile-sized working atlas into the tile's sub-rect of the
    // page-sized display atlas (both left shader-readable).
    void record_display_publish(erhe::graphics::Command_buffer& command_buffer, int tile);
    // Overwrite freshly assigned display slots with white (the unbaked
    // look): a new occupant's regions map into the slot immediately, but
    // the slot still holds the previous occupant's published texels until
    // the first publish/restore. Consumes m_slots_pending_white_clear;
    // called by tick() after ensure_bake_targets.
    void record_pending_slot_white_clears(erhe::graphics::Command_buffer& command_buffer);
    // One-shot virtual-offset pass (article sample-position adjustment):
    // rewrites the position G-buffer in place, once per G-buffer bake.
    // bind_instance_records binds the Lm_instance_record SSBO (binding 1)
    // into the adjust encoder - the committed_face_normal position-fetch
    // fallback reads it (the caller owns the buffer, ring range or plain).
    void record_adjust(
        erhe::graphics::Command_buffer&                                       command_buffer,
        erhe::graphics::Acceleration_structure&                               tlas,
        const std::function<void(erhe::graphics::Compute_command_encoder&)>&  bind_instance_records
    );

    class Blas_entry
    {
    public:
        std::shared_ptr<erhe::primitive::Primitive>            primitive; // keeps the Buffer_mesh alive
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
    };
    auto get_or_create_blas(
        erhe::graphics::Command_buffer&                    command_buffer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const erhe::primitive::Buffer_mesh&                buffer_mesh
    ) -> erhe::graphics::Acceleration_structure*;

    erhe::graphics::Device&                            m_graphics_device;
    erhe::scene_renderer::Mesh_memory&                 m_mesh_memory;
    Atlas_layout                                       m_layout;
    int                                                m_tile_size  {s_tile};
    int                                                m_slot_budget{s_default_slot_budget};
    float                                              m_cell_size  {8.0f};
    std::vector<glm::ivec3>                            m_tile_overrides;
    Lightmap_report*                                   m_report     {nullptr};
    const Lightmap_partitioner*                        m_partitioner{nullptr};
    bool                                               m_show_tile_bounds{false};

    // G-buffer raster pass objects (created once in the constructor).
    std::unique_ptr<erhe::graphics::Shader_resource>   m_draw_block; // per-draw UBO: world_from_node + uv_scale_offset + jitter + base_color
    std::size_t                                        m_draw_block_world_offset     {0};
    std::size_t                                        m_draw_block_uv_offset        {0};
    std::size_t                                        m_draw_block_jitter_offset    {0};
    std::size_t                                        m_draw_block_base_color_offset{0};
    std::size_t                                        m_draw_block_size             {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>  m_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_shader_stages;
    // Two raster variants: plain, and (when the extension exists) with
    // native conservative rasterization; bake_gbuffer() picks per
    // Bake_options::coverage_mode.
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_pipeline;
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_pipeline_conservative;
    std::shared_ptr<erhe::graphics::Texture>           m_position_texture;
    std::shared_ptr<erhe::graphics::Texture>           m_normal_texture;
    // Phong-tessellation smooth position (article terminator fix); consumed
    // by the one-shot adjust pass, which folds the validated result into
    // m_position_texture, so the gather never reads this directly.
    std::shared_ptr<erhe::graphics::Texture>           m_smooth_position_texture;
    bool                                               m_gbuffer_valid{false};
    bool                                               m_gbuffer_adjusted{false}; // virtual-offset pass ran for this G-buffer
    // VK_EXT_conservative_rasterization is available (m_pipeline_conservative
    // exists); Coverage_mode::conservative falls back to jitter_9 otherwise.
    bool                                               m_conservative_supported{false};

    // Texel supersampling (Frostbite Flux, slide "Texel sampling (2)"):
    // ray-origin positions rasterized WITHOUT conservative raster at
    // c_supersample_factor x atlas resolution, so each covered hi-res texel
    // is a true on-triangle sample point of the regular sub-texel grid.
    // Two shader variants mirror the adjust pass: with and without the
    // Phong-tessellated smooth position (Bake_options::terminator_fix).
    std::unique_ptr<erhe::graphics::Fragment_outputs>  m_origin_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_origin_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_origin_pipeline;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_origin_no_smooth_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_origin_no_smooth_pipeline;
    std::shared_ptr<erhe::graphics::Texture>           m_origin_texture;
    // The origin texture holds a valid supersample raster for the current
    // G-buffer (option on AND the hi-res page fit the size guard);
    // m_origin_factor is the grid side it was rasterized at (4 or 8) and
    // selects the matching gather variant.
    bool                                               m_origin_valid{false};
    int                                                m_origin_factor{0};

    // Virtual-offset adjust pass (article sample-position adjustment). Two
    // variants: with and without the terminator smooth-position adoption,
    // selected per bake by Bake_options::terminator_fix.
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_adjust_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_adjust_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_adjust_pipeline;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_adjust_no_smooth_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_adjust_no_smooth_pipeline;

    // Direct-light gather objects.
    std::unique_ptr<erhe::graphics::Shader_resource>   m_gather_block;
    std::size_t                                        m_gather_light_count_offset   {0};
    std::size_t                                        m_gather_ray_bias_offset      {0};
    std::size_t                                        m_gather_frame_index_offset   {0};
    std::size_t                                        m_gather_base_y_offset        {0};
    std::size_t                                        m_gather_bounce_enabled_offset{0};
    std::size_t                                        m_gather_sky_enabled_offset   {0};
    std::size_t                                        m_gather_sun_direction_offset {0};
    std::size_t                                        m_gather_sky_params_offset    {0};
    std::size_t                                        m_gather_position_type_offset {0};
    std::size_t                                        m_gather_direction_cos_offset {0};
    std::size_t                                        m_gather_radiance_range_offset{0};
    std::size_t                                        m_gather_params_offset        {0};
    std::size_t                                        m_gather_block_size           {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_gather_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_gather_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_gather_pipeline;
    // Supersampled-origin gather variants (ERHE_LM_SUPERSAMPLE > 0): build
    // the per-texel valid-origin list from m_origin_texture and pick a
    // random entry per ray. One variant per grid side (4 = 16 points,
    // 8 = 64 points), compiled up front so
    // Bake_options::supersample_factor switches without a shader rebuild.
    std::unique_ptr<erhe::graphics::Shader_stages>     m_gather_ss4_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_gather_ss4_pipeline;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_gather_ss8_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_gather_ss8_pipeline;
    std::unique_ptr<erhe::graphics::Sampler>           m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture>           m_lightmap_texture;
    bool                                               m_lightmap_valid{false};

    // Double-buffered publish: the renderer-facing page-sized fp16 atlas
    // (see get_lightmap_texture). Tiles copy their tile-sized working
    // result into their page sub-rect at publish points; per-tile
    // Tile_state::published tracks which tiles hold a complete sweep
    // (until then the first-bake progressive preview copies every
    // publish of that tile).
    std::shared_ptr<erhe::graphics::Texture>           m_display_texture;

    // Dilation pass (plan phase 4): valid texels flood into invalid
    // 8-neighborhood, s_padding iterations, ping-pong between the atlas
    // and a scratch texture, so bilinear sampling at chart edges never
    // reads unbaked (black) texels.
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_dilate_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_dilate_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_dilate_pipeline;
    std::shared_ptr<erhe::graphics::Texture>           m_dilate_texture;

    std::unordered_map<const erhe::primitive::Buffer_mesh*, Blas_entry> m_blas_cache;
    std::unique_ptr<erhe::graphics::Acceleration_structure>             m_tlas;
    uint32_t                                                            m_tlas_capacity{0};

    // ---- Interactive bake loop state (plan section 3a) ----

    // Extra gather inputs: G-buffer albedo (bounce modulation + future JNLM
    // guide), the per-tile accumulation textures (rgb = radiance sum, w =
    // sample count; tile-sized fp32, allocated lazily per visited tile and
    // released for camera-clamped inactive tiles), and per-instance records
    // for bounce-ray attribute fetch.
    std::shared_ptr<erhe::graphics::Texture>           m_albedo_texture;
    class Tile_state
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> accum;
        bool     accum_dirty{true};  // clear before the next gather (fresh texture or reset)
        bool     published  {false}; // display holds at least one complete sweep of this tile
        bool     active     {true};  // within the camera clamp; inactive tiles skip gathering
        bool     has_content{false}; // at least one region packed into this tile
        // Published content newer than the last save-to-disk. With
        // save-on-evict enabled, the residency swap defers evicting such a
        // tile until the owner drains take_tile_pending_save().
        bool     dirty_since_save{false};
        // One disk-restore attempt per activation: set when the tile is
        // queued for take_tile_pending_restore() (or when an invalidation
        // makes the disk content stale), re-armed on eviction.
        bool     restore_attempted{false};
        uint32_t sweeps     {0};     // completed tile sweeps since reset
        // restore_tile(): the display slot holds a disk payload of this
        // many sweeps; republish is suppressed until fresh accumulation
        // reaches it, so the restored content never regresses to an
        // early-sweep result. 0 = no hold (normal publish cadence).
        uint32_t restore_hold_sweeps{0};
    };
    std::vector<Tile_state>                            m_tiles;
    bool                                               m_save_on_evict{false};
    std::vector<int>                                   m_tiles_pending_save;
    std::vector<int>                                   m_tiles_pending_restore;
    // Freshly assigned display slots awaiting their white overwrite
    // (record_pending_slot_white_clears); a successful disk restore
    // unqueues its slot, a full-page clear empties the list.
    std::vector<int>                                   m_slots_pending_white_clear;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_lm_instance_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_lm_instance_block;
    std::unique_ptr<erhe::graphics::Sampler>           m_linear_sampler;

    // Resolve pass (accum -> published average); same i_src / i_dst shape
    // as dilate but its own layout: fp32 accumulation in, fp16 atlas out.
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_resolve_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_resolve_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_resolve_pipeline;

    // JNLM denoise (plan phase 4): published atlas + G-buffer normal/albedo
    // guides in, denoised atlas out (into the dilate scratch); runs per
    // completed sweep, folded into record_resolve_and_dilate.
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_denoise_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_denoise_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_denoise_pipeline;

    // Seam blend pass (article: standard per-publish step; reference Godot
    // lm_blendseams): each UV seam edge is drawn as two atlas-space lines,
    // one per side, sampling the OPPOSITE side's radiance from a copy of
    // the published atlas and alpha-blending 0.5 over it, pulling the two
    // sides together. Seam edges are found on the GEO mesh where shared
    // vertex ids make the test exact: a facet edge whose second occurrence
    // carries different channel-2 UVs is a seam (equal corner normals
    // required, so hard edges with genuinely discontinuous lighting are
    // not blended).
    class Seam_vertex
    {
    public:
        glm::vec2 position;  // atlas UV of this side's edge endpoint
        glm::vec2 source_uv; // atlas UV of the opposite side's endpoint
    };
    // Partitioned-mode layout: regions come from the partitioner's piece
    // meshes with pre-assigned tiles; the tile tree is the stored partition
    // (never re-split from live pieces). Called by update_layout() after
    // its reset preamble.
    auto update_layout_partitioned(Scene_root& scene_root, float min_face_texels) -> bool;
    // Uniform quadtree grid split (world-origin anchored, shared by
    // update_layout and compute_tile_split_estimate): occupied level-0
    // cells from the regions' world XZ AABBs, scene leaf overrides
    // applied, kd tree emitted as two planes per quadtree split so
    // clip_by_tile_tree consumes it unchanged. tiles is index-aligned with
    // the kd leaves' tile indices; unoccupied quadrants become tile -1
    // leaves (no geometry routes there - occupancy is AABB-conservative).
    class Grid_split
    {
    public:
        std::vector<erhe::geometry::operation::Clip_tree_node>              kd_nodes;
        std::vector<Tile>                                                   tiles;
        std::unordered_map<Lightmap_tile_key, int, Lightmap_tile_key_hash>  tile_of_key;
        [[nodiscard]] auto tile_for_position(glm::vec2 xz, float base_cell_size) const -> int;
    };
    auto build_grid_split(const std::vector<erhe::math::Aabb>& region_bounds) -> Grid_split;

    // Skyline-pack one grid tile's regions at the tile's nominal density,
    // flexing density down only (error + drop below the 1% floor). Fills
    // rect + uv_scale_offset of the packed regions, appends them to
    // out_packed_regions and records the flex on the tile.
    void pack_tile_regions(
        int                             tile_index,
        Tile&                           tile,
        std::vector<Instance_region>&   regions,
        const std::vector<std::size_t>& members,
        float                           min_face_texels,
        std::vector<Instance_region>&   out_packed_regions
    );
    // Shared tail of both layout paths: display slot grid sizing against
    // the memory budget, m_layout/m_tiles assignment, initial residency and
    // seam vertices.
    auto finalize_layout(
        std::vector<Tile>&&                                       tiles,
        std::vector<Instance_region>&&                            regions,
        std::vector<erhe::geometry::operation::Clip_tree_node>&&  kd_nodes,
        bool                                                      partitioned
    ) -> bool;
    void build_seam_vertices();
    void record_seam_blend(erhe::graphics::Command_buffer& command_buffer, int tile);
    erhe::dataformat::Vertex_format                     m_seam_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_seam_vertex_input;
    std::unique_ptr<erhe::graphics::Bind_group_layout>  m_seam_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>   m_seam_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_seam_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>    m_seam_pipeline;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_seam_vertex_ring;
    // Ordered by tile: m_tile_seam_ranges[tile] = (first vertex, count)
    // into m_seam_vertices, positions relative to the tile origin so the
    // pass rasters into the tile-sized working atlas.
    std::vector<Seam_vertex>                            m_seam_vertices;
    std::vector<std::pair<uint32_t, uint32_t>>          m_tile_seam_ranges;

    // Per-tick uploads ride the device ring buffers (the frame command
    // buffer stays open; plain buffers would be destroyed in flight).
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_tick_gather_ubo;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_tick_instance_ssbo;

    // bake_direct() is a standalone submit (wait_idle before return), so
    // plain buffers are safe there; kept as members so a previous call's
    // buffers outlive their submission.
    std::unique_ptr<erhe::graphics::Buffer>            m_direct_gather_ubo;
    std::unique_ptr<erhe::graphics::Buffer>            m_direct_instance_ssbo;

    // Per-frame-in-flight TLAS slots for tick() (rebuilding the single
    // TLAS a still-in-flight frame reads would be a data race; mirrors
    // Ray_trace_renderer). bake_direct() keeps using m_tlas (wait_idle).
    static constexpr std::size_t s_tlas_slot_count = 4;
    class Tlas_slot
    {
    public:
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
        uint32_t                                                capacity{0};
    };
    std::array<Tlas_slot, s_tlas_slot_count>           m_tlas_slots;

    // Offline bake-to-disk state (start_offline_bake / offline_tick).
    class Offline_state
    {
    public:
        Offline_progress  progress{};
        int               next_tile{0};
        Offline_tile_sink sink{};
    };
    Offline_state m_offline_state{};

    Bake_options m_options{};
    Sky_lighting m_sky{};
    bool     m_baking_enabled   {false};
    bool     m_reset_requested  {false};
    // Single Iteration (request_single_iteration): pause the bake once the
    // minimum active-tile sweep count reaches the target.
    bool     m_pause_after_sweep{false};
    uint32_t m_pause_target_sweeps{0};
    bool     m_publish_requested{false}; // publish-stage option changed; republish on next tick
    bool     m_display_cleared  {false}; // display atlas zeroed at least once for this layout
    bool     m_regions_published{false}; // per-primitive uv_scale_offset pushed to meshes
    int      m_gbuffer_tile     {-1};    // tile the (tile-sized) G-buffer targets hold; -1 = none
    int      m_cursor_tile      {0};     // tile the gather is currently sweeping
    int      m_cursor_y         {0};     // next band start row within the cursor tile
    uint32_t m_frame_counter    {0};     // RNG decorrelation across ticks
    bool     m_hashes_initialized{false};
    // Layout invalidations land in the same frame as the primitive swap
    // that caused them, but the swapped meshes' vertex uploads sit in the
    // FRAME command buffer (submitted at end of frame) while the G-buffer
    // bake is a standalone submit that would run first - rastering
    // not-yet-copied buffers into permanently black regions. Set on layout
    // change; the tick skips one G-buffer bake so the upload-carrying frame
    // is submitted ahead of the bake on the same queue.
    bool     m_gbuffer_upload_defer{false};
    uint64_t m_hash_lighting    {0};     // lights + occluder transforms
    uint64_t m_hash_gbuffer     {0};     // lightmapped region transforms
    uint64_t m_hash_layout      {0};     // lightmapped set + grid parameters
    Scene_root* m_layout_scene_root{nullptr}; // scene update_layout() ran for

    // Per-tick scratch (capacity kept across frames).
    std::vector<erhe::graphics::Acceleration_structure_instance> m_tick_instances;
    std::vector<Lm_instance_record>                              m_tick_records;
};

} // namespace editor
