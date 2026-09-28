#pragma once

#include "app_message.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/probe_grid.hpp"
#include "renderers/radiance_cascades_layout.hpp"
#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_command_encoder;
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
struct Ddgi_config;
struct Radiance_cascades_config;
enum class Radiance_cascades_merge_mode : unsigned int;

namespace editor {

class App_context;
class App_message_bus;
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
    trace           = 0,
    merge           = 1,
    reduce          = 2,
    neighbour_trace = 3  // merge mode per_neighbour_trace only: the connecting segments
};
constexpr std::size_t c_rc_pass_count = 4;

// World-space radiance cascades (doc/editor/radiance_cascades.md,
// doc/plans/radiance_cascades.md): the second producer of the indirect
// diffuse probe field, next to Ddgi_renderer.
//
// Fits the cascades to the padded content bounding box
// (fit_radiance_cascades()), allocates each cascade's raw and merged
// radiance atlases, and traces the raw intervals (rc_trace.comp) under a
// per-frame texel budget, then merges every cascade with everything beyond
// it (rc_merge.comp, top cascade down to cascade 0), and reduces merged
// cascade 0 into the probe field atlases of the DDGI format
// (rc_reduce.comp): the field the forward pass samples while this source
// is selected (get_field()).
//
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// tick() does nothing.
class Radiance_cascades_renderer
{
public:
    // The two atlases of one cascade (RGBA16F, rgb radiance, a transparency
    // beta; storage + sampled). raw holds the traced intervals, merged the
    // raw intervals merged with everything beyond them.
    // state is one texel per probe (tile coordinates: probe_index wrapped
    // into rows of tiles_per_row), R32F holding an integer-valued float,
    // the probe state of the visibility pass (c_state_* bits); allocated
    // only in the visibility_masked merge mode.
    // neighbours holds, per raw texel, the visibilities of the 8 connecting
    // segments of the per_neighbour_trace merge mode (RGBA16F, one channel
    // each, a 2 x 1 texel block per raw texel, segment n in channel n & 3 of
    // (2 x + (n >> 2), y)); allocated only in that mode, for every cascade
    // but the top one. Cascade 0's merged atlas has 2 x 2 texels per raw
    // texel (the merge at cascade 1's angular resolution).
    class Cascade_textures
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> raw;
        std::shared_ptr<erhe::graphics::Texture> merged;
        std::shared_ptr<erhe::graphics::Texture> state;
        std::shared_ptr<erhe::graphics::Texture> neighbours;
    };

    // Probe state bits (rc_visibility.comp, rc_merge.comp): bits 0 - 7 = the
    // segment from the probe to upper probe n of get_upper_probes() order is
    // unobstructed, bit 8 = the probe is inside geometry.
    static constexpr uint32_t c_state_upper_visible_mask = 0xffu;
    static constexpr uint32_t c_state_inside             = 0x100u;

    // GPU time of one pass: the most recent measurement and the mean over
    // the last c_timing_history_size measurements, in milliseconds.
    class Pass_time
    {
    public:
        double last_ms   {0.0};
        double average_ms{0.0};
    };

    // Measured cost of the trace, merge and reduce
    // (doc/plans/radiance_cascades.md section 8). GPU timings lag the
    // recorded update by the frames in flight.
    class Stats
    {
    public:
        Pass_time trace{};
        Pass_time neighbour_trace{};             // merge mode per_neighbour_trace: the connecting segments
        Pass_time merge{};
        Pass_time reduce{};
        Pass_time total{};                       // trace + neighbour_trace + merge + reduce
        uint64_t  update_count            {0};   // ticks that dispatched the trace (and the merge and reduce)
        uint64_t  timing_sample_count     {0};   // GPU timing samples taken
        uint64_t  completed_sweeps        {0};   // full passes of the cursor over all texels since the atlases were allocated
        int64_t   texels_per_update       {0};   // the budget clamped to the total texel count
        // Rays per update: one interval ray per texel, plus in the
        // per_neighbour_trace merge mode the connecting segments (up to 8
        // per texel of every cascade but the top one) - the mean over a
        // full sweep, since the cursor's runs cross cascades.
        int64_t   rays_per_update         {0};
        int64_t   neighbour_rays_per_update{0};  // the connecting segments of rays_per_update
        int64_t   updates_per_full_refresh{0};   // ticks until every texel is traced once
        double    ms_per_million_rays     {0.0}; // total.average_ms per 1e6 rays_per_update
        double    full_refresh_ms         {0.0}; // updates_per_full_refresh x total.average_ms
        // Visibility pass: runs only when the layout or the scene geometry
        // changed, so it has no per-frame mean; the last measurement.
        double    visibility_last_ms      {0.0};
        uint64_t  visibility_update_count {0};   // visibility pass runs since construction
        uint64_t  history_reset_count     {0};   // temporal history resets (allocations and change messages)
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
        // Visibility pass: probes inside geometry, and the mean fraction of
        // the 8 upper probes whose segment from the probe is unobstructed.
        int       inside_probe_count  {0};
        float     upper_visible_fraction{0.0f};
    };

    static constexpr std::size_t c_timing_history_size = 60;

    // Radiance_cascades_config::debug_cascade_mask: bit i masks cascade i,
    // this bit masks the sky beyond the top cascade.
    static constexpr int c_sky_mask_bit = c_max_radiance_cascades;

    // ddgi_config supplies the field's sampling parameters (irradiance /
    // distance texels, depth sharpness, biases, intensity), so both
    // producers' fields render the same way.
    Radiance_cascades_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        App_message_bus&                         app_message_bus,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        const Radiance_cascades_config&          config,
        const Ddgi_config&                       ddgi_config,
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
    // Active and the probe field atlases exist: the renderer writes the
    // field the forward pass samples.
    [[nodiscard]] auto has_field   () const -> bool;
    // The field (get_indirect_diffuse_field()): cascade 0's grid, the
    // sampling parameters of Ddgi_config and the three atlases rc_reduce.comp
    // writes. Invalid unless has_field().
    [[nodiscard]] auto get_field             () const -> Probe_field;
    [[nodiscard]] auto get_forward_parameters() const -> erhe::scene_renderer::Ddgi_parameters;

    // The merge mode (Radiance_cascades_config::merge_mode). Called by the
    // change sites (Radiance Cascades window combo, MCP
    // set_radiance_cascades) after they store the setting; the constructor
    // takes the loaded setting. visibility_masked allocates and computes
    // the probe states on the next tick, the other modes release them.
    // Switching into or out of per_neighbour_trace refits on the next tick:
    // that mode's neighbour atlases are part of the layout (the atlas fit
    // leaves room for them) and start with a fresh first fill.
    void               set_merge_mode(Radiance_cascades_merge_mode mode);
    [[nodiscard]] auto get_merge_mode() const -> Radiance_cascades_merge_mode;

    [[nodiscard]] auto get_layout                   () const -> const Radiance_cascades_layout&;
    [[nodiscard]] auto get_cascade_textures         (int cascade) const -> const Cascade_textures&;
    // Cascade 0 hit distance statistics per raw texel (RGBA32F, same layout
    // as the cascade 0 raw atlas): mean distance, mean squared distance,
    // backface fraction, blended with the raw texel's hysteresis.
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
    // rgb merged radiance, a merged transparency of one texel (cascade 0:
    // the mean of its child texels).
    [[nodiscard]] auto read_merged_texel          (int cascade, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;
    // Cascade 0 is merged at cascade 1's angular resolution: child
    // (0 .. c_merged_cascade0_block^2 - 1, x + block * y) of a cascade 0
    // texel is the texel merged with the upper value of cascade 1 texel
    // (block * u + x, block * v + y).
    [[nodiscard]] auto read_merged_child_texel    (const glm::ivec3& probe, const glm::ivec2& texel, int child) const -> glm::vec4;
    // Cascade 0 hit distance statistics of one texel: x mean distance, y
    // mean squared distance, z backface fraction.
    [[nodiscard]] auto read_distance_statistics   (const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;
    // The snapshot holds the probe field atlases (irradiance, distance,
    // probe data), copied with the parameters get_readback_field_parameters()
    // reports.
    [[nodiscard]] auto readback_has_field           () const -> bool;
    [[nodiscard]] auto get_readback_field_parameters() const -> const erhe::scene_renderer::Ddgi_parameters&;
    // Interior texel (u, v) of a probe's irradiance tile (rgb) or distance
    // tile (mean, mean squared distance), and the probe data texel (xyz
    // offset, w state); readback_has_field() must be true.
    [[nodiscard]] auto read_field_irradiance_texel(const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec3;
    [[nodiscard]] auto read_field_distance_texel  (const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec2;
    [[nodiscard]] auto read_field_probe_data      (const glm::ivec3& probe) const -> glm::vec4;
    // The snapshot holds probe states (the visibility_masked mode was active
    // at copy time).
    [[nodiscard]] auto readback_has_probe_states  () const -> bool;
    // Probe state (c_state_* bits) of one probe; readback_has_probe_states()
    // must be true.
    [[nodiscard]] auto read_probe_state           (int cascade, const glm::ivec3& probe) const -> uint32_t;

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
    // trace, the merge of all cascades and the reduce into the probe field.
    // Editor::tick() calls it only while radiance cascades is the selected
    // source. Must be called outside a render pass.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

private:
    // Refits + reallocates when needed. Returns false when there is no
    // usable volume this tick.
    [[nodiscard]] auto update_layout    (erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool;
    void               allocate_textures(erhe::graphics::Command_buffer& command_buffer);
    void               release_textures ();
    // Probe state textures of the visibility_masked merge mode.
    void               allocate_state_textures(erhe::graphics::Command_buffer& command_buffer);
    void               release_state_textures ();
    // Probe field atlases of cascade 0's grid and the reduce weight buffer,
    // (re)created when the layout or the field's sampling settings change.
    void               allocate_field         (erhe::graphics::Command_buffer& command_buffer);
    void               release_field          ();

    // Records the visibility pass (rc_visibility.comp): per probe of every
    // cascade, whether it is inside geometry and which of its 8 upper probes
    // it can see. Geometry and layout only, so it runs when either changed
    // (m_visibility_dirty), not per frame.
    void record_visibility(
        erhe::graphics::Command_buffer&          command_buffer,
        const Scene_tlas::Frame&                 tlas_frame,
        const erhe::graphics::Ring_buffer_range& light_range,
        erhe::scene_renderer::Material_set&      material_set
    );

    // Records the budgeted trace dispatches: the texel cursor walks the
    // cascades in order, one dispatch per contiguous run of one cascade.
    // The runs are kept in m_trace_runs for record_neighbour_trace().
    void record_trace(
        erhe::graphics::Command_buffer&          command_buffer,
        const Scene_tlas::Frame&                 tlas_frame,
        const erhe::graphics::Ring_buffer_range& light_range,
        erhe::scene_renderer::Material_set&      material_set
    );
    // Merge mode per_neighbour_trace: traces the connecting segments of the
    // texel runs record_trace() traced this frame (rc_trace.comp,
    // ERHE_RC_TRACE_NEIGHBOURS), for every cascade but the top one, into
    // the neighbour atlases; timed as its own pass.
    void record_neighbour_trace(
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
    // Reduces merged cascade 0 into the probe field atlases (rc_reduce.comp):
    // the irradiance variant writes the irradiance atlas and the probe data,
    // the distance variant the distance atlas; one workgroup per probe.
    void record_reduce(erhe::graphics::Command_buffer& command_buffer);
    // Copies every raw and merged atlas and the distance texture into the
    // readback buffer (request_texel_readback()).
    void record_texel_readback(erhe::graphics::Command_buffer& command_buffer);
    // One RGBA16F texel of the snapshot atlas copied at atlas_offset.
    [[nodiscard]] auto read_radiance_texel(std::size_t atlas_offset, int cascade, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4;

    // Binds the previous probe field to a trace dispatch (the field
    // samplers every trace variant declares).
    void bind_trace_field(erhe::graphics::Compute_command_encoder& encoder);

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
    // Live reference to the editor's Ddgi_config: the field's sampling
    // parameters.
    const Ddgi_config&              m_ddgi_config;
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
    // The connecting segments one full sweep traces in the
    // per_neighbour_trace mode (per probe of every cascade but the top one,
    // its upper probes of nonzero weight x q_i^2); 0 in the other modes.
    int64_t                                                  m_neighbour_rays_per_sweep{0};
    std::size_t                                              m_texture_byte_count{0};
    uint64_t                                                 m_fit_count{0};

    // Trace cursor: the next texel in the global order (cascade 0 first).
    // A sweep completes when it wraps. The blend of each traced texel
    // follows m_history (an allocation resets it: the first sweep after it
    // is written unblended).
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
    // The per_neighbour_trace connecting segments (writes the neighbour
    // atlas only).
    Trace_pass                                                m_trace_neighbours;
    // The texel runs of this frame's trace, replayed by the neighbour
    // trace; cleared per frame, capacity kept.
    class Trace_run
    {
    public:
        int        cascade      {0};
        int64_t    first_texel  {0};
        int64_t    count        {0};
        glm::uvec4 history      {0u};    // rc_trace.history of the run (Temporal_history::get_shader_parameters())
        glm::uvec4 run          {0u};    // rc_trace.run: global first texel, jitter seed, jitter mode
        bool       needs_barrier{false}; // a run of the same cascade precedes it this frame
    };
    std::vector<Trace_run>                                    m_trace_runs;
    std::unique_ptr<erhe::scene_renderer::Light_buffer>       m_light_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_projections>  m_light_projections;
    uint32_t                                                  m_tlas_binding_point    {0};
    uint32_t                                                  m_raw_binding_point     {0};
    uint32_t                                                  m_distance_binding_point{0};
    uint32_t                                                  m_neighbours_binding_point{0};

    // Control block field offsets, resolved once at construction.
    class Control_offsets
    {
    public:
        std::size_t grid_origin {0};
        std::size_t grid_spacing{0};
        std::size_t grid_counts {0};
        std::size_t dispatch     {0};
        std::size_t params       {0};
        std::size_t upper_origin {0};
        std::size_t upper_spacing{0};
        std::size_t upper_counts {0};
        std::size_t history      {0};
        std::size_t run          {0};
    };
    Control_offsets m_control_offsets{};
    // The field's sampler (Light_interface::ddgi_sampler): the trace samples
    // the previous field at hits when bounces is multi.
    const erhe::graphics::Sampler*                            m_field_sampler{nullptr};
    uint32_t                                                  m_field_irradiance_binding_point{0};
    uint32_t                                                  m_field_distance_binding_point  {0};
    uint32_t                                                  m_field_probe_data_binding_point{0};

    // Temporal history of the raw texels (and the neighbour visibilities):
    // reset on every allocation and by the change messages
    // (doc/editor/ddgi.md "History reset").
    Temporal_history                                          m_history{};
    erhe::message_bus::Subscription<Scene_lighting_changed_message> m_scene_lighting_changed_subscription;
    // Per-update direction jitter seed (Radiance_cascades_direction_jitter).
    std::mt19937                                              m_random_engine{0x5eed5eedu};

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
    // rc_merge.comp variants: interpolate (ERHE_RC_MERGE_MODE 0),
    // visibility_masked (1) and per_neighbour_trace (2).
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_merge_visibility_shader_stages;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_merge_neighbours_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_merge_pipeline;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_merge_visibility_pipeline;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_merge_neighbours_pipeline;
    Radiance_cascades_merge_mode                              m_merge_mode;

    // Visibility pass (rc_visibility.comp): the trace's buffers and TLAS,
    // the cascade's state texture as the only storage image. Dirty after
    // every allocation and on the scene geometry change messages.
    erhe::graphics::Shader_resource                           m_visibility_block;
    class Visibility_offsets
    {
    public:
        std::size_t grid_origin  {0};
        std::size_t grid_spacing {0};
        std::size_t grid_counts  {0};
        std::size_t upper_origin {0};
        std::size_t upper_spacing{0};
        std::size_t upper_counts {0};
    };
    Visibility_offsets                                        m_visibility_offsets{};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_visibility_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_visibility_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_visibility_pipeline;
    uint32_t                                                  m_state_binding_point{0};
    std::unique_ptr<erhe::graphics::Gpu_timer>                m_visibility_timer;
    uint64_t                                                  m_visibility_last_ns     {0};
    uint64_t                                                  m_visibility_update_count{0};
    bool                                                      m_visibility_dirty       {true};
    erhe::message_bus::Subscription<Node_touched_message>          m_node_touched_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Items_removed_message>         m_items_removed_subscription;

    // Probe field (rc_reduce.comp): the three DDGI-format atlases for
    // cascade 0's grid, the settings they were allocated with, and the
    // reduce weights - per output texel the lobe integral over each input
    // texel footprint (compute_octahedral_lobe_weights()), cosine over the
    // merged cascade 0 child texels for irradiance, pow(cos,
    // depth_sharpness) over the cascade 0 texels for distance, each stored
    // as a sparse list per output texel (the inputs the lobe reaches), then
    // the cascade 0 texel solid angles - packed four floats per vec4 in one
    // read-only storage buffer, created on change only.
    class Field_settings
    {
    public:
        int   irradiance_texels{0};
        int   distance_texels  {0};
        float depth_sharpness  {0.0f};

        [[nodiscard]] auto operator==(const Field_settings& other) const -> bool = default;
    };
    Field_settings                                            m_field_settings{};
    int                                                       m_field_tiles_per_row{0};
    std::shared_ptr<erhe::graphics::Texture>                  m_field_irradiance;
    std::shared_ptr<erhe::graphics::Texture>                  m_field_distance;
    std::shared_ptr<erhe::graphics::Texture>                  m_field_probe_data;
    std::size_t                                               m_field_byte_count{0};
    std::unique_ptr<erhe::graphics::Buffer>                   m_reduce_weights_buffer;
    std::size_t                                               m_reduce_weights_byte_count{0};
    uint32_t                                                  m_reduce_irradiance_weights_offset{0}; // in floats
    uint32_t                                                  m_reduce_distance_weights_offset  {0};
    uint32_t                                                  m_reduce_solid_angle_offset       {0};
    erhe::graphics::Shader_resource                           m_reduce_block;
    erhe::graphics::Shader_resource                           m_reduce_weights_block;
    class Reduce_offsets
    {
    public:
        std::size_t grid_counts{0};
        std::size_t params     {0};
        std::size_t weights    {0};
        std::size_t limits     {0};
    };
    Reduce_offsets                                            m_reduce_offsets{};
    class Reduce_pass
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout>        bind_group_layout;
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages> shader_stages;
        std::unique_ptr<erhe::graphics::Compute_pipeline>         pipeline;
    };
    Reduce_pass                                               m_reduce_irradiance;
    Reduce_pass                                               m_reduce_distance;

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
    std::array<std::size_t, c_max_radiance_cascades>         m_readback_state_offsets{};
    bool                                                     m_readback_has_states{false};
    bool                                                     m_readback_has_field {false};
    erhe::scene_renderer::Ddgi_parameters                    m_readback_field_parameters{};
    std::size_t                                              m_readback_field_irradiance_offset{0};
    std::size_t                                              m_readback_field_distance_offset  {0};
    std::size_t                                              m_readback_field_probe_data_offset{0};
    int                                                      m_readback_field_irradiance_width {0};
    int                                                      m_readback_field_distance_width   {0};
    int                                                      m_readback_field_probe_data_width {0};
    std::size_t                                              m_readback_byte_count{0};
    std::vector<std::byte>                                   m_readback_snapshot;
    std::array<Cascade_summary, c_max_radiance_cascades>     m_readback_summaries{};
};

} // namespace editor
