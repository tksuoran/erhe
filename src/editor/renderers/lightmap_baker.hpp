#pragma once

#include "erhe_dataformat/vertex_format.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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
    // the rect is the content region in texels (padding lives outside it).
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
        // Tile cell (page-space grid of s_tile x s_tile cells) the region
        // packs into; regions never span cells, so the bake working set
        // (G-buffer, working atlas, accumulation) only ever needs one
        // cell's worth of texels at a time.
        int                                cell{0};
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
    };

    class Atlas_layout
    {
    public:
        int                          width {0};
        int                          height{0};
        // Page-space tile grid: cells_x * cells_y cells of s_tile texels
        // (1x1 when the page is not larger than a tile). Cell index i maps
        // to page texel origin ((i % cells_x) * s_tile, (i / cells_x) * s_tile).
        int                          cells_x{1};
        int                          cells_y{1};
        std::vector<Instance_region> regions;

        [[nodiscard]] auto get_cell_count () const -> int { return cells_x * cells_y; }
        [[nodiscard]] auto get_cell_origin(int cell) const -> glm::ivec2;
        // Cell side in texels: min(page side, s_tile).
        [[nodiscard]] auto get_cell_size  () const -> int;
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

    [[nodiscard]] auto is_supported() const -> bool;

    // True when the ray-query gather machinery exists - false without
    // Device_info::use_ray_query. Layout and G-buffer (is_supported) still
    // work then; only the bakes are unavailable.
    [[nodiscard]] auto is_bake_supported() const -> bool;

    // Recompute the atlas layout for the lightmapped, non-skinned content
    // meshes of the scene whose primitives carry channel-2 UVs. Page size
    // grows in power-of-two steps until everything packs (up to s_max_page
    // texels). Returns true when at least one region was packed.
    // min_face_texels > 0 additionally grows each region (capped at 4x the
    // density-derived side) so its smallest facet spans at least that many
    // texels on its shorter UV axis.
    auto update_layout(Scene_root& scene_root, float texels_per_meter, float min_face_texels) -> bool;

    [[nodiscard]] auto get_layout() const -> const Atlas_layout& { return m_layout; }

    // Rasterize the texel G-buffer for one tile cell of the current layout:
    // one draw per region of the cell, positions mapped through channel-2
    // UVs into the region's rect relative to the cell origin. The G-buffer
    // targets are cell-sized and hold exactly one cell at a time
    // (get_gbuffer_cell). Standalone submit (own command buffer + wait
    // idle). Returns false when there is no layout or the pipeline is
    // unavailable.
    auto bake_gbuffer(int cell = 0) -> bool;
    [[nodiscard]] auto get_gbuffer_cell() const -> int { return m_gbuffer_cell; }

    // Debug: write the current cell's G-buffer as 8-bit PNGs
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
    // result to Make_atlas_operation's per-facet chart order so a
    // re-unwrap packs similarly lit facets next to each other - cross-
    // chart filter-tap / dilation pollution then picks up similar values.
    // Empty when no bake exists. Keyed by the CURRENT primitive geometry
    // (the re-unwrap's operation source).
    auto build_chart_order_keys() -> std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>;

    // Interactive bake loop (plan section 3a): record one budgeted gather
    // slice plus resolve + dilate publish into the given command buffer
    // (the open frame command buffer; the rendergraph samples the published
    // atlas later in the same submission). Handles change-driven
    // invalidation internally: light/occluder edits reset accumulation,
    // lightmapped-mesh edits additionally re-raster the G-buffer.
    //
    // The gather walks the atlas one tile cell at a time (the working set
    // is cell-sized); max_active_cells > 0 with a valid camera position
    // limits gathering to the N cells whose regions are nearest the
    // camera - the display atlas keeps showing the other cells' last
    // publish, and their accumulation textures are released.
    void tick(
        erhe::graphics::Command_buffer& command_buffer,
        Scene_root&                     scene_root,
        float                           texels_per_meter,
        float                           min_face_texels,
        const glm::vec3*                camera_position  = nullptr,
        int                             max_active_cells = 0
    );

    // Disabling releases the whole bake working set (G-buffer, atlas,
    // accumulation, scratch, origin, BLAS/TLAS caches) - only the display
    // atlas the forward renderer samples stays resident. Re-enabling
    // rebuilds everything and restarts accumulation from scratch.
    void set_baking_enabled(bool enabled);
    [[nodiscard]] auto is_baking_enabled() const -> bool     { return m_baking_enabled; }
    void request_reset() { m_reset_requested = true; }
    // Completed accumulation sweeps since the last reset: the minimum over
    // the active cells' per-cell sweep counts, so every ACTIVE valid texel
    // holds at least this many samples.
    [[nodiscard]] auto get_sweep_count() const -> uint32_t;
    [[nodiscard]] auto get_cursor_row () const -> int        { return m_cursor_y; }
    [[nodiscard]] auto get_cursor_cell() const -> int        { return m_cursor_cell; }

    // Display atlas the forward renderer samples: a copy of the working
    // atlas taken only at publish points (complete sweeps), so bake resets
    // (transform / light edits) keep showing the previous result instead of
    // blacking out the scene while accumulation restarts.
    [[nodiscard]] auto get_lightmap_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_display_texture; }

    [[nodiscard]] auto get_position_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_position_texture; }
    [[nodiscard]] auto get_normal_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_normal_texture; }
    [[nodiscard]] auto get_albedo_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_albedo_texture; }

    static constexpr int s_min_page = 256;
    // Page ceiling, not a hardware limit (Metal / desktop Vulkan allow
    // 16384^2). Pages grow power-of-two only as needed, and update_layout()
    // additionally caps the page so the persistent per-texel targets
    // (accumulation + display, ~24 bytes per texel) fit the device's
    // remaining memory budget.
    static constexpr int s_max_page = 8192;
    // Tile cell side. Pages larger than this are packed as a grid of
    // cells; regions never span a cell, and the bake scratch targets
    // (G-buffer x4, working atlas, dilate scratch, supersample origins)
    // are cell-sized - the gather visits one cell at a time - so scratch
    // cost is bounded at ~60 bytes x s_tile^2 (~250 MB) regardless of the
    // page size. Per-cell fp32 accumulation textures allocate lazily and
    // camera clamping (tick max_active_cells) releases the far ones.
    static constexpr int s_tile     = 2048;
    static constexpr int s_padding  = 4; // texels around each region (mips + bilinear)

private:
    void ensure_gbuffer_targets();
    void ensure_bake_targets(erhe::graphics::Command_buffer& command_buffer);
    // Lazily create (and clear when dirty) the given cell's accumulation
    // texture; returns it. Records the clear into command_buffer.
    auto ensure_cell_accum(erhe::graphics::Command_buffer& command_buffer, int cell) -> erhe::graphics::Texture*;
    void publish_regions();
    // Free every rebuildable working-set allocation, keeping only the
    // display atlas (see set_baking_enabled).
    void release_working_set();

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
    // Record resolve (cell accum -> working running average), optionally
    // the JNLM denoise, then the dilation ping-pong; leaves the working
    // atlas shader-readable. Operates on the cell-sized targets; the
    // G-buffer must hold the same cell (denoise guides).
    void record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer, int cell, bool with_denoise);
    // Copy the cell-sized working atlas into the cell's sub-rect of the
    // page-sized display atlas (both left shader-readable).
    void record_display_publish(erhe::graphics::Command_buffer& command_buffer, int cell);
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
    // (see get_lightmap_texture). Cells copy their cell-sized working
    // result into their page sub-rect at publish points; per-cell
    // Cell_state::published tracks which cells hold a complete sweep
    // (until then the first-bake progressive preview copies every
    // publish of that cell).
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
    // guide), the per-cell accumulation textures (rgb = radiance sum, w =
    // sample count; cell-sized fp32, allocated lazily per visited cell and
    // released for camera-clamped inactive cells), and per-instance records
    // for bounce-ray attribute fetch.
    std::shared_ptr<erhe::graphics::Texture>           m_albedo_texture;
    class Cell_state
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> accum;
        bool     accum_dirty{true};  // clear before the next gather (fresh texture or reset)
        bool     published  {false}; // display holds at least one complete sweep of this cell
        bool     active     {true};  // within the camera clamp; inactive cells skip gathering
        bool     has_content{false}; // at least one region packed into this cell
        uint32_t sweeps     {0};     // completed cell sweeps since reset
    };
    std::vector<Cell_state>                            m_cells;
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
    void build_seam_vertices();
    void record_seam_blend(erhe::graphics::Command_buffer& command_buffer, int cell);
    erhe::dataformat::Vertex_format                     m_seam_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_seam_vertex_input;
    std::unique_ptr<erhe::graphics::Bind_group_layout>  m_seam_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>   m_seam_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_seam_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>    m_seam_pipeline;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_seam_vertex_ring;
    // Ordered by cell: m_cell_seam_ranges[cell] = (first vertex, count)
    // into m_seam_vertices, positions relative to the cell origin so the
    // pass rasters into the cell-sized working atlas.
    std::vector<Seam_vertex>                            m_seam_vertices;
    std::vector<std::pair<uint32_t, uint32_t>>          m_cell_seam_ranges;

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

    Bake_options m_options{};
    Sky_lighting m_sky{};
    bool     m_baking_enabled   {false};
    bool     m_reset_requested  {false};
    bool     m_publish_requested{false}; // publish-stage option changed; republish on next tick
    bool     m_display_cleared  {false}; // display atlas zeroed at least once for this layout
    bool     m_regions_published{false}; // per-primitive uv_scale_offset pushed to meshes
    int      m_gbuffer_cell     {-1};    // cell the (cell-sized) G-buffer targets hold; -1 = none
    int      m_cursor_cell      {0};     // cell the gather is currently sweeping
    int      m_cursor_y         {0};     // next band start row within the cursor cell
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
    uint64_t m_hash_layout      {0};     // lightmapped set + texel density
    float    m_layout_texels_per_meter{0.0f};
    Scene_root* m_layout_scene_root{nullptr}; // scene update_layout() ran for

    // Per-tick scratch (capacity kept across frames).
    std::vector<erhe::graphics::Acceleration_structure_instance> m_tick_instances;
    std::vector<Lm_instance_record>                              m_tick_records;
};

} // namespace editor
