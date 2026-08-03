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
        std::vector<Instance_region> regions;
    };

    // Optional bake features (Lightmap window checkboxes; viewport bicubic
    // sampling is not here - it lives in the forward renderer). set_options()
    // reacts to changes: terminator_fix re-rasters the G-buffer, indirect
    // bounce restarts accumulation, publish-stage toggles (denoise, dilation,
    // seam blend) force a republish on the next tick.
    class Bake_options
    {
    public:
        bool  indirect_bounce{true};
        bool  terminator_fix {true};
        bool  denoise        {true};
        bool  dilation       {true};
        bool  seam_blend     {true};
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

    // Rasterize the texel G-buffer for the current layout: one draw per
    // region, positions mapped through channel-2 UVs into the region's
    // atlas rect. Standalone submit (own command buffer + wait idle) -
    // fine for the phase-2 milestone; phase 3 folds this into the
    // interactive per-frame loop. Returns false when there is no layout
    // or the pipeline is unavailable.
    auto bake_gbuffer() -> bool;

    // Debug: write the G-buffer as 8-bit PNGs (<base>_position.png with
    // position mapped into the layout bounds, <base>_normal.png as
    // normal * 0.5 + 0.5; alpha = coverage). Requires bake_gbuffer().
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
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root, float texels_per_meter, float min_face_texels);

    void set_baking_enabled(const bool enabled) { m_baking_enabled = enabled; }
    [[nodiscard]] auto is_baking_enabled() const -> bool     { return m_baking_enabled; }
    void request_reset() { m_reset_requested = true; }
    // Completed full-atlas accumulation sweeps since the last reset; every
    // valid texel holds at least this many samples.
    [[nodiscard]] auto get_sweep_count() const -> uint32_t   { return m_sweep_count; }
    [[nodiscard]] auto get_cursor_row () const -> int        { return m_cursor_y; }

    // Display atlas the forward renderer samples: a copy of the working
    // atlas taken only at publish points (complete sweeps), so bake resets
    // (transform / light edits) keep showing the previous result instead of
    // blacking out the scene while accumulation restarts.
    [[nodiscard]] auto get_lightmap_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_display_texture; }

    [[nodiscard]] auto get_position_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_position_texture; }
    [[nodiscard]] auto get_normal_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_normal_texture; }
    [[nodiscard]] auto get_albedo_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_albedo_texture; }

    static constexpr int s_min_page = 256;
    // Working-set budget, not a hardware limit (Metal / desktop Vulkan allow
    // 16384^2): seven page-sized targets exist during a bake (~104 bytes per
    // texel), so 8192^2 costs ~7 GB when a scene actually packs that big.
    // Pages grow power-of-two only as needed; going past 8192 should first
    // slim the rgba32f targets or add multi-page support.
    static constexpr int s_max_page = 8192;
    static constexpr int s_padding  = 4; // texels around each region (mips + bilinear)

private:
    void ensure_gbuffer_targets();
    void ensure_bake_targets(erhe::graphics::Command_buffer& command_buffer);
    void publish_regions();

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
    // Record resolve (accum -> published running average), optionally the
    // JNLM denoise, then the dilation ping-pong; leaves the published atlas
    // shader-readable.
    void record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer, bool with_denoise);
    // Blit the working atlas into the display atlas (both left
    // shader-readable) and mark the display valid.
    void record_display_publish(erhe::graphics::Command_buffer& command_buffer);
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
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_pipeline;
    std::shared_ptr<erhe::graphics::Texture>           m_position_texture;
    std::shared_ptr<erhe::graphics::Texture>           m_normal_texture;
    // Phong-tessellation smooth position (article terminator fix); consumed
    // by the one-shot adjust pass, which folds the validated result into
    // m_position_texture, so the gather never reads this directly.
    std::shared_ptr<erhe::graphics::Texture>           m_smooth_position_texture;
    bool                                               m_gbuffer_valid{false};
    bool                                               m_gbuffer_adjusted{false}; // virtual-offset pass ran for this G-buffer
    // VK_EXT_conservative_rasterization active on the G-buffer pipeline:
    // one raster pass; false = 9-tap jitter fallback.
    bool                                               m_conservative_raster{false};

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
    std::unique_ptr<erhe::graphics::Sampler>           m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture>           m_lightmap_texture;
    bool                                               m_lightmap_valid{false};

    // Double-buffered publish: the renderer-facing copy of the working
    // atlas (see get_lightmap_texture). m_display_valid = it holds at
    // least one complete sweep; until then the first-bake progressive
    // preview copies every publish.
    std::shared_ptr<erhe::graphics::Texture>           m_display_texture;
    bool                                               m_display_valid{false};

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
    // guide), the accumulation atlas (rgb = radiance sum, w = sample
    // count), and per-instance records for bounce-ray attribute fetch.
    std::shared_ptr<erhe::graphics::Texture>           m_albedo_texture;
    std::shared_ptr<erhe::graphics::Texture>           m_accum_texture;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_lm_instance_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_lm_instance_block;
    std::unique_ptr<erhe::graphics::Sampler>           m_linear_sampler;

    // Resolve pass (accum -> published average); shares m_dilate_layout
    // (two rgba32f storage images named i_src / i_dst).
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
    void record_seam_blend(erhe::graphics::Command_buffer& command_buffer);
    erhe::dataformat::Vertex_format                     m_seam_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_seam_vertex_input;
    std::unique_ptr<erhe::graphics::Bind_group_layout>  m_seam_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>   m_seam_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_seam_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>    m_seam_pipeline;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_seam_vertex_ring;
    std::vector<Seam_vertex>                            m_seam_vertices;

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
    bool     m_accum_cleared    {false}; // accumulation + published atlas zeroed at least once
    bool     m_regions_published{false}; // per-primitive uv_scale_offset pushed to meshes
    int      m_cursor_y         {0};     // next band start row (tile cursor)
    uint32_t m_sweep_count      {0};     // completed full-atlas sweeps since reset
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
