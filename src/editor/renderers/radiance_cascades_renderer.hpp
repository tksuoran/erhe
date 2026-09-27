#pragma once

#include "renderers/indirect_diffuse.hpp"
#include "renderers/probe_grid.hpp"
#include "renderers/radiance_cascades_layout.hpp"
#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Gpu_timer;
    class Reloadable_shader_stages;
    class Ring_buffer_client;
    class Sampler;
    class Texture;
}
namespace erhe::scene_renderer {
    class Light_buffer;
    class Light_projections;
    class Material_set;
    class Mesh_memory;
    class Program_interface;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Radiance_cascades_config;

namespace editor {

class App_context;
class Scene_root;

// State of the request-driven raw texel readback
// (Radiance_cascades_renderer::request_texel_readback()).
enum class Rc_readback_state : unsigned int
{
    idle      = 0, // nothing requested
    requested = 1, // copy is recorded after the next trace
    in_flight = 2, // copy recorded, frame not retired yet
    complete  = 3  // snapshot readable
};

// Which atlas of a cascade the Radiance Cascades window preview shows.
enum class Rc_preview_source : unsigned int
{
    raw    = 0, // the traced intervals
    merged = 1  // the intervals merged with everything beyond them
};

// What the Radiance Cascades window preview shows of an atlas.
enum class Rc_preview_channel : unsigned int
{
    radiance = 0, // rgb radiance
    beta     = 1, // transparency as grey
    distance = 2  // cascade 0 signed hit distance (green front face, red backface)
};

// The timed GPU passes of one radiance cascades update.
enum class Rc_pass : unsigned int
{
    trace = 0,
    merge = 1
};
constexpr std::size_t c_rc_pass_count = 2;

// World-space radiance cascades (doc/editor/radiance_cascades.md,
// doc/plans/radiance_cascades.md): the second producer of the indirect
// diffuse probe field, next to Ddgi_renderer.
//
// Fits the cascades to the padded content bounding box
// (fit_radiance_cascades()), allocates each cascade's raw and merged
// radiance atlases, and traces the raw intervals (rc_trace.comp) under a
// per-frame texel budget, then merges every cascade with everything beyond
// it (rc_merge.comp, top cascade down to cascade 0). The reduce pass is a
// later phase of the plan: until it exists the renderer produces no probe
// field (has_field() is false) and the forward pass keeps the flat ambient
// term while this source is selected.
//
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// tick() does nothing.
class Radiance_cascades_renderer
{
public:
    // The two atlases of one cascade (RGBA16F, rgb radiance, a transparency
    // beta; storage + sampled). raw holds the traced intervals, merged the
    // raw intervals merged with everything beyond them.
    class Cascade_textures
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> raw;
        std::shared_ptr<erhe::graphics::Texture> merged;
    };

    // GPU time of one pass: the most recent measurement and the mean over
    // the last c_timing_history_size measurements, in milliseconds.
    class Pass_time
    {
    public:
        double last_ms   {0.0};
        double average_ms{0.0};
    };

    // Measured cost of the trace and merge (doc/plans/radiance_cascades.md
    // section 8). GPU timings lag the recorded update by the frames in
    // flight.
    class Stats
    {
    public:
        Pass_time trace{};
        Pass_time merge{};
        Pass_time total{};                       // trace + merge
        uint64_t  update_count            {0};   // ticks that dispatched the trace (and the merge)
        uint64_t  timing_sample_count     {0};   // GPU timing samples taken
        uint64_t  completed_sweeps        {0};   // full passes of the cursor over all texels since the atlases were allocated
        int64_t   texels_per_update       {0};   // the budget clamped to the total texel count
        int64_t   rays_per_update         {0};   // one interval ray per texel
        int64_t   updates_per_full_refresh{0};   // ticks until every texel is traced once
        double    ms_per_million_rays     {0.0}; // total.average_ms per 1e6 rays_per_update
        double    full_refresh_ms         {0.0}; // updates_per_full_refresh x total.average_ms
    };

    // Per-cascade summary of a texel readback (the probe texels only, not
    // the unused tiles of a partly filled last atlas row).
    class Cascade_summary
    {
    public:
        int64_t   texel_count         {0};
        glm::vec3 mean_radiance       {0.0f}; // raw
        float     beta_one_fraction   {0.0f}; // raw texels with beta > 0.5 (the interval is mostly empty)
        glm::vec3 mean_merged_radiance{0.0f}; // merged
        float     mean_merged_beta    {0.0f}; // merged transparency: the fraction of the ray that escapes the top cascade
        // Cascade 0 only: texels whose last trace hit a backface, and the
        // probes with at least one such texel.
        float     backface_fraction   {0.0f};
        int       backface_probe_count{0};
    };

    static constexpr std::size_t c_timing_history_size = 60;

    // Radiance_cascades_config::debug_cascade_mask: bit i masks cascade i,
    // this bit masks the sky beyond the top cascade.
    static constexpr int c_sky_mask_bit = c_max_radiance_cascades;

    Radiance_cascades_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        const Radiance_cascades_config&          config,
        Producer_selection                       selection
    );
    ~Radiance_cascades_renderer() noexcept;

    [[nodiscard]] auto is_supported() const -> bool;
    // Radiance cascades is the selected indirect diffuse source.
    [[nodiscard]] auto is_selected () const -> bool;
    // Called by set_indirect_diffuse_source() only. Deselecting releases the
    // atlases, the layout and the trace timings; selecting leaves the fit to
    // the next tick.
    void               set_selection(Producer_selection selection);
    // Selected AND supported AND a layout was fitted and allocated.
    [[nodiscard]] auto is_active   () const -> bool;
    // The renderer writes the probe field the forward pass samples. False
    // until the reduce pass exists (plan phase 4).
    [[nodiscard]] auto has_field   () const -> bool;

    [[nodiscard]] auto get_layout                   () const -> const Radiance_cascades_layout&;
    [[nodiscard]] auto get_cascade_textures         (int cascade) const -> const Cascade_textures&;
    // Cascade 0 signed hit distance per raw texel (R32F, same layout as the
    // cascade 0 raw atlas).
    [[nodiscard]] auto get_distance_texture         () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    // Bytes of one cascade's textures (cascade 0 includes the distance
    // texture), and of all cascades.
    [[nodiscard]] auto get_cascade_texture_byte_count(int cascade) const -> std::size_t;
    [[nodiscard]] auto get_texture_byte_count       () const -> std::size_t;
    // Layout refits since construction (each one reallocates the atlases).
    [[nodiscard]] auto get_fit_count                () const -> uint64_t;
    [[nodiscard]] auto get_stats                    () const -> Stats;

    // Texel readback (MCP get_radiance_cascades_texels,
    // doc/editor/radiance_cascades.md "MCP"). request_texel_readback() asks
    // for a copy of every raw and merged atlas and the cascade 0 distance
    // texture after the next trace and merge (no-op while one is in
    // flight); poll_texel_readback()
    // takes the copy into a CPU snapshot once its frame retired. Nothing is
    // copied unless requested.
    void               request_texel_readback     ();
    [[nodiscard]] auto poll_texel_readback        () -> Rc_readback_state;
    // Valid while poll_texel_readback() reports complete. The layout the
    // snapshot was copied with, and the counters at copy time.
    [[nodiscard]] auto get_readback_layout        () const -> const Radiance_cascades_layout&;
    [[nodiscard]] auto get_readback_update_count  () const -> uint64_t;
    [[nodiscard]] auto get_readback_sweep_count   () const -> uint64_t;
    [[nodiscard]] auto get_readback_summary       (int cascade) const -> const Cascade_summary&;
    // rgb radiance, a beta of one raw texel; probe coordinates and tile
    // texel must be inside the snapshot layout.
    [[nodiscard]] auto read_raw_texel             (int cascade, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;
    // rgb merged radiance, a merged transparency of one texel.
    [[nodiscard]] auto read_merged_texel          (int cascade, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;
    // Cascade 0 signed hit distance of one texel.
    [[nodiscard]] auto read_distance_texel        (const glm::ivec3& probe, const glm::ivec2& texel) const -> float;

    // Atlas preview for the Radiance Cascades window. The atlases carry
    // beta in alpha, which the ImGui image widget would use as opacity, so
    // the window shows an opaque copy (rc_preview.comp) instead.
    // request_preview() asks for one to be recorded after the next trace
    // and merge; the window calls it each frame it shows the preview, so
    // the copy is only made while someone looks at it. The distance channel
    // always shows cascade 0.
    void               request_preview    (int cascade, Rc_preview_source source, Rc_preview_channel channel, float radiance_scale);
    [[nodiscard]] auto get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;

    // Refits the cascades and reallocates the atlases when the content
    // bounds or a fit setting changed, then records this frame's budgeted
    // trace and the merge of all cascades. Editor::tick() calls it only
    // while radiance cascades is the selected source. Must be called
    // outside a render pass.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

private:
    // Refits + reallocates when needed. Returns false when there is no
    // usable volume this tick.
    [[nodiscard]] auto update_layout    (erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool;
    void               allocate_textures(erhe::graphics::Command_buffer& command_buffer);
    void               release_textures ();

    // Records the budgeted trace dispatches: the texel cursor walks the
    // cascades in order, one dispatch per contiguous run of one cascade.
    void record_trace(
        erhe::graphics::Command_buffer&          command_buffer,
        const Scene_tlas::Frame&                 tlas_frame,
        const erhe::graphics::Ring_buffer_range& light_range,
        erhe::scene_renderer::Material_set&      material_set
    );
    // Merges every cascade with everything beyond it (rc_merge.comp): one
    // dispatch per cascade, top cascade first, each reading the raw atlas
    // written by this frame's trace and the merged atlas of the cascade
    // above written by the previous dispatch.
    void record_merge(erhe::graphics::Command_buffer& command_buffer, const glm::vec3& sky_radiance);
    // Copies every raw and merged atlas and the distance texture into the
    // readback buffer (request_texel_readback()).
    void record_texel_readback(erhe::graphics::Command_buffer& command_buffer);
    // One RGBA16F texel of the snapshot atlas copied at atlas_offset.
    [[nodiscard]] auto read_radiance_texel(std::size_t atlas_offset, int cascade, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;

    // Writes the requested opaque atlas preview (request_preview()).
    void record_preview(erhe::graphics::Command_buffer& command_buffer);

    // Takes each pass timer's latest result into its history. Called once
    // per update, before the update records its own timestamps.
    void sample_pass_timings();
    void clear_pass_timings ();

    erhe::graphics::Device&         m_graphics_device;
    App_context&                    m_context;
    // Live reference to the editor's Radiance_cascades_config
    // (editor_settings.radiance_cascades).
    const Radiance_cascades_config& m_config;
    Producer_selection              m_selection{Producer_selection::deselected};
    bool                            m_supported{false};

    // The settings the current layout was fitted with; any change refits.
    Radiance_cascades_layout_settings m_fit_settings{};
    float                             m_fit_padding_m{-1.0f};

    Radiance_cascades_layout                                 m_layout{};
    Probe_volume_bounds                                      m_volume_bounds{};
    std::array<Cascade_textures, c_max_radiance_cascades>    m_cascade_textures{};
    std::shared_ptr<erhe::graphics::Texture>                 m_distance_texture;
    std::array<std::size_t,      c_max_radiance_cascades>    m_cascade_byte_counts{};
    // First texel of each cascade in the global texel order the cursor walks.
    std::array<int64_t,          c_max_radiance_cascades>    m_cascade_texel_offsets{};
    std::size_t                                              m_texture_byte_count{0};
    uint64_t                                                 m_fit_count{0};

    // Trace cursor: the next texel in the global order (cascade 0 first).
    // A sweep completes when it wraps; during the first sweep after an
    // allocation every traced texel is new, so it is written unblended.
    int64_t  m_texel_cursor    {0};
    uint64_t m_completed_sweeps{0};
    int64_t  m_texels_per_update{0};

    // GPU side. Own Scene_tlas, light buffer and projections, like
    // Ddgi_renderer: the trace shades hits with the same
    // erhe_ray_hit.glsl / erhe_ddgi_ray.glsl path.
    std::unique_ptr<Scene_tlas>                               m_scene_tlas;
    erhe::graphics::Shader_resource                           m_control_block;
    std::unique_ptr<erhe::graphics::Ring_buffer_client>       m_control_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_trace_bind_group_layout;
    // rc_trace.comp variants: cascade 0 (writes the distance texture) and
    // the upper cascades (do not reference it).
    class Trace_pass
    {
    public:
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages> shader_stages;
        std::unique_ptr<erhe::graphics::Compute_pipeline>         pipeline;
    };
    Trace_pass                                                m_trace_cascade0;
    Trace_pass                                                m_trace_upper;
    std::unique_ptr<erhe::scene_renderer::Light_buffer>       m_light_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_projections>  m_light_projections;
    uint32_t                                                  m_tlas_binding_point    {0};
    uint32_t                                                  m_raw_binding_point     {0};
    uint32_t                                                  m_distance_binding_point{0};

    // Control block field offsets, resolved once at construction.
    class Control_offsets
    {
    public:
        std::size_t grid_origin {0};
        std::size_t grid_spacing{0};
        std::size_t grid_counts {0};
        std::size_t dispatch    {0};
        std::size_t params      {0};
    };
    Control_offsets m_control_offsets{};

    // Merge (rc_merge.comp): its own control block and layout, the shared
    // control ring buffer. The raw atlas and the upper merged atlas are
    // sampled (texelFetch, nearest sampler), the merged atlas written is the
    // only storage image.
    erhe::graphics::Shader_resource                           m_merge_block;
    class Merge_offsets
    {
    public:
        std::size_t grid_counts {0};
        std::size_t upper_counts{0};
        std::size_t params      {0};
        std::size_t sky         {0};
    };
    Merge_offsets                                             m_merge_offsets{};
    std::unique_ptr<erhe::graphics::Sampler>                  m_merge_sampler;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_merge_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_merge_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_merge_pipeline;

    // Atlas preview (rc_preview.comp): its own control block and layout,
    // the shared control ring buffer.
    erhe::graphics::Shader_resource                           m_preview_block;
    std::size_t                                               m_preview_size_offset  {0};
    std::size_t                                               m_preview_params_offset{0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_preview_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_preview_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_preview_pipeline;
    std::shared_ptr<erhe::graphics::Texture>                  m_preview_texture;
    bool                                                      m_preview_requested     {false};
    int                                                       m_preview_cascade       {0};
    Rc_preview_source                                         m_preview_source        {Rc_preview_source::raw};
    Rc_preview_channel                                        m_preview_channel       {Rc_preview_channel::radiance};
    float                                                     m_preview_radiance_scale{1.0f};

    // Pass timing: one timer around all of a tick's dispatches of a pass
    // (Rc_pass), and a fixed ring of its recent results.
    class Pass_timing
    {
    public:
        std::unique_ptr<erhe::graphics::Gpu_timer>  timer;
        std::array<uint64_t, c_timing_history_size> history_ns{};
        std::size_t                                 history_count{0};
        std::size_t                                 history_next {0};
        uint64_t                                    last_ns      {0};
    };
    std::array<Pass_timing, c_rc_pass_count> m_pass_timings;
    uint64_t                                 m_update_count       {0};
    uint64_t                                 m_timing_sample_count{0};

    // Texel readback. The buffer is (re)allocated on request only; the
    // snapshot vectors are filled on the MCP path, never per frame.
    std::unique_ptr<erhe::graphics::Buffer>                  m_readback_buffer;
    Rc_readback_state                                        m_readback_state      {Rc_readback_state::idle};
    uint64_t                                                 m_readback_frame      {0};
    uint64_t                                                 m_readback_update_count{0};
    uint64_t                                                 m_readback_sweep_count{0};
    Radiance_cascades_layout                                 m_readback_layout{};
    std::array<std::size_t, c_max_radiance_cascades>         m_readback_raw_offsets{};
    std::array<std::size_t, c_max_radiance_cascades>         m_readback_merged_offsets{};
    std::size_t                                              m_readback_distance_offset{0};
    std::size_t                                              m_readback_byte_count{0};
    std::vector<std::byte>                                   m_readback_snapshot;
    std::array<Cascade_summary, c_max_radiance_cascades>     m_readback_summaries{};
};

} // namespace editor
