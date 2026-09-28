#pragma once

#include "app_message.hpp"
#include "renderable.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/probe_grid.hpp"
#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
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
    class Shader_stage_extension;
    class Texture;
    class Texture_heap;
}
namespace erhe::scene_renderer {
    class Material_buffer;
    class Mesh_memory;
    class Material_set;
    class Program_interface;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Ddgi_config;

namespace editor {

class App_context;
class App_message_bus;
class Render_context;
class Scene_root;

// The GPU passes of one DDGI update, each timed separately.
enum class Ddgi_pass : unsigned int
{
    trace            = 0,
    blend_irradiance = 1,
    blend_distance   = 2,
    relocate         = 3
};
constexpr std::size_t c_ddgi_pass_count = 4;

[[nodiscard]] auto c_str(Ddgi_pass pass) -> const char*;

// One world-space point an irradiance query evaluates (MCP
// sample_indirect_diffuse, doc/editor/ddgi.md "Irradiance queries").
// view_direction points from the surface toward the viewer, exactly like
// standard.frag's V; normal and view_direction are unit length.
class Irradiance_query_point
{
public:
    glm::vec3 position      {0.0f};
    glm::vec3 normal        {0.0f, 1.0f, 0.0f};
    glm::vec3 view_direction{0.0f, 1.0f, 0.0f};
};

enum class Irradiance_query_state : unsigned int
{
    idle      = 0, // nothing requested, or the last result was taken
    queued    = 1, // points accepted, dispatch not recorded yet
    in_flight = 2, // dispatch recorded, frame not retired yet
    complete  = 3, // results readable
    failed    = 4  // could not be recorded; the reference query reports why
};

// One point of a reference irradiance query (MCP reference_indirect_diffuse,
// doc/editor/ddgi.md "Reference irradiance"). irradiance has the units and
// the convention of ddgi_sample_irradiance(): cosine-weighted mean incident
// radiance (E / pi), times the configured DDGI intensity.
class Reference_irradiance_sample
{
public:
    glm::vec3 irradiance       {0.0f};
    float     standard_error   {0.0f}; // of the luminance of irradiance
    float     sky_fraction     {0.0f}; // rays that escaped (sky radiance)
    float     backface_fraction{0.0f}; // rays that hit a backface (zero radiance)
};

// Arguments of one reference irradiance query.
class Reference_query_settings
{
public:
    int      rays_per_point{4096};
    uint32_t seed          {1u};
    float    normal_bias   {0.01f}; // ray origin offset along the normal, metres
};

// Dynamic diffuse global illumination (doc/editor/ddgi.md).
//
// One scene-wide probe volume, auto-fitted to the padded content bounding
// box. Probes are traced with ray queries into a ray data texture, blended
// into octahedral irradiance and distance atlases with temporal hysteresis,
// and sampled by the forward shader in place of the flat ambient term.
//
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// tick() does nothing. All probe state is held in 2D textures because the
// graphics abstraction only exposes image2D storage images.
//
// Milestone status: phase 3 - grid fit, texture allocation and the probe
// trace pass. The blend / relocation passes land in phases 4-5.
class Ddgi_renderer : public Renderable
{
public:
    // The fitted probe grid (fit_probe_grid()). spacing is per axis: the
    // padded box extent divided by (counts - 1), so the first and last probe
    // planes sit exactly on the box faces.
    using Grid = Probe_grid;

    // GPU time of one pass: the most recent measurement and the mean over
    // the last c_timing_history_size measurements, in milliseconds.
    class Pass_time
    {
    public:
        double last_ms   {0.0};
        double average_ms{0.0};
    };

    // Measured cost of the probe updates (doc/plans/radiance_cascades.md
    // section 8). GPU timings lag the recorded update by the frames in
    // flight.
    class Stats
    {
    public:
        std::array<Pass_time, c_ddgi_pass_count> passes{};
        Pass_time   total{};                      // sum of the passes
        uint64_t    update_count            {0};  // ticks that dispatched the probe update
        uint64_t    timing_sample_count     {0};  // GPU timing samples taken (all passes at once)
        int64_t     rays_per_update         {0};  // probes_per_update x rays_per_probe, as dispatched
        int         updates_per_full_refresh{0};  // ticks until every probe is traced once
        double      ms_per_million_rays     {0.0}; // total.average_ms per 1e6 rays_per_update
        double      full_refresh_ms         {0.0}; // updates_per_full_refresh x total.average_ms
        uint64_t    history_reset_count     {0};  // temporal history resets (allocations and change messages)
    };

    static constexpr std::size_t c_timing_history_size = 60;

    // Upper bound on the points of one irradiance query.
    static constexpr std::size_t c_max_irradiance_query_points = 4096;

    // Reference irradiance query bounds: rays per point, rays per query
    // (points x rays per point), and the rays one frame traces - the query
    // is split into per-frame chunks of whole points so no single
    // submission runs long enough to risk a GPU timeout.
    static constexpr int         c_max_reference_rays_per_point = 65536;
    static constexpr int64_t     c_max_reference_rays_per_query = int64_t{1} << 26;
    static constexpr int64_t     c_reference_rays_per_frame     = int64_t{1} << 21;

    Ddgi_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        App_message_bus&                         app_message_bus,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        const Ddgi_config&                       config,
        Producer_selection                       selection
    );
    ~Ddgi_renderer() noexcept;

    [[nodiscard]] auto is_supported() const -> bool;
    // Selected as the indirect diffuse source AND supported AND a usable
    // grid was fitted.
    [[nodiscard]] auto is_active   () const -> bool;
    // DDGI is the selected indirect diffuse source.
    [[nodiscard]] auto is_selected () const -> bool;
    // Called by set_indirect_diffuse_source() only. Deselecting releases the
    // probe textures, the grid and the pass timings; selecting leaves the
    // refit to the next tick.
    void               set_selection(Producer_selection selection);

    [[nodiscard]] auto get_grid                    () const -> const Grid&;
    [[nodiscard]] auto get_irradiance_texture      () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_distance_texture        () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_probe_data_texture      () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_ray_data_texture        () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_texture_byte_count      () const -> std::size_t;
    // Rays per probe actually used (the configured value rounded up to the
    // trace workgroup size), and the interior octahedral resolutions.
    [[nodiscard]] auto get_rays_per_probe          () const -> int;
    [[nodiscard]] auto get_irradiance_texels       () const -> int;
    [[nodiscard]] auto get_distance_texels         () const -> int;
    // Probes updated per tick (config budget clamped to the grid), and how
    // many ticks one full sweep of the grid therefore takes.
    [[nodiscard]] auto get_probes_per_update       () const -> int;
    [[nodiscard]] auto get_instance_count          () const -> std::size_t;
    [[nodiscard]] auto get_stats                   () const -> Stats;

    // The probe volume parameters the forward pass samples with: the fitted
    // grid plus the sampling settings. Invalid (counts 0) unless active.
    [[nodiscard]] auto get_forward_parameters      () const -> erhe::scene_renderer::Ddgi_parameters;
    // The field DDGI publishes (get_indirect_diffuse_field()): the forward
    // parameters and the three atlases. Invalid unless active.
    [[nodiscard]] auto get_field                   () const -> Probe_field;

    // Irradiance query: evaluates ddgi_sample_irradiance() - the forward
    // pass's function, with the forward pass's parameters - at world points
    // on the GPU and reads the linear float results back
    // (doc/editor/ddgi.md "Irradiance queries"). It samples the published
    // field of whichever producer is selected (get_indirect_diffuse_field()),
    // so it serves radiance cascades too. One query at a time:
    // begin_irradiance_query() accepts the points (false when a query is
    // queued or in flight, or when there are more than
    // c_max_irradiance_query_points); record_irradiance_query() records the
    // dispatch into the frame after the probe update; poll_irradiance_query()
    // reports the state and reads the results back once that frame retired.
    [[nodiscard]] auto begin_irradiance_query      (std::span<const Irradiance_query_point> points) -> bool;
    void               cancel_irradiance_query     ();
    [[nodiscard]] auto poll_irradiance_query       () -> Irradiance_query_state;
    // Valid while poll_irradiance_query() reports complete: one rgb per
    // point, and the update_count of the field that was sampled.
    [[nodiscard]] auto get_irradiance_query_results     () const -> std::span<const glm::vec3>;
    [[nodiscard]] auto get_irradiance_query_update_count() const -> uint64_t;
    // Records a queued query against field, the field Editor::tick()
    // publishes this frame. Called once per frame after the producers'
    // ticks, outside a render pass; no-op unless a query is queued and the
    // field is valid.
    void record_irradiance_query(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root, const Probe_field& field);

    // Reference irradiance query: a Monte Carlo estimate of the irradiance
    // at world points with the exact light transport of a DDGI probe ray
    // (erhe_ddgi_ray.glsl), cosine-distributed rays from each point, so it
    // differs from sample_indirect_diffuse only by the probe field's
    // discretization (doc/editor/ddgi.md "Reference irradiance"). Runs
    // whether DDGI is enabled or not (needs ray query support). tick()
    // records it in per-frame chunks of c_reference_rays_per_frame rays;
    // one query at a time, same state machine as the irradiance query plus
    // failed (get_reference_query_error() says why).
    [[nodiscard]] auto begin_reference_query        (std::span<const Irradiance_query_point> points, const Reference_query_settings& settings) -> bool;
    void               cancel_reference_query       ();
    [[nodiscard]] auto poll_reference_query         () -> Irradiance_query_state;
    [[nodiscard]] auto get_reference_query_results  () const -> std::span<const Reference_irradiance_sample>;
    [[nodiscard]] auto get_reference_query_error    () const -> const std::string&;
    // The DDGI intensity the results include.
    [[nodiscard]] auto get_reference_query_intensity() const -> float;

    // Probe relocation / classification state, read back on request
    // (doc/editor/ddgi.md "Probe state"). request_probe_states() asks for a
    // copy of the probe data texture after the next probe update (no-op
    // while one is in flight); poll_probe_states() reads a retired copy into
    // the snapshot and reports whether the snapshot is valid for the current
    // grid. The snapshot holds one vec4 per probe, indexed like
    // probe_index = x + counts.x * (y + counts.y * z): xyz the relocation
    // offset in world units, w the state (1 active, 0 inactive).
    class Probe_state_summary
    {
    public:
        uint64_t update_count           {0};    // field updates when the probe data was copied
        int      active                 {0};
        int      inactive               {0};
        int      relocated              {0};    // |offset| > c_relocated_offset_m
        float    max_offset_over_spacing{0.0f}; // largest |offset| / smallest grid spacing
    };
    static constexpr float c_relocated_offset_m = 1.0e-3f;

    void               request_probe_states       ();
    [[nodiscard]] auto poll_probe_states          () -> bool;
    [[nodiscard]] auto get_probe_state_summary    () const -> const Probe_state_summary&;
    [[nodiscard]] auto get_probe_states           () const -> std::span<const glm::vec4>;

    // Refits the grid, reallocates the probe textures when needed, and
    // records this tick's probe update into the command buffer while DDGI
    // is the selected source, plus the next chunk of a pending reference
    // query whatever the source. Must be called outside a render pass. No-op
    // unless supported.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

    // Implements Renderable: the probe overlay (volume box + one wire
    // sphere per probe, coloured by classification state and offset by the
    // relocation the GPU applied). Drawn only when debug_draw_probes is on.
    void render(const Render_context& context) override;

private:
    // The two blend passes share one source (ddgi_blend.comp, switched by
    // ERHE_DDGI_BLEND_DISTANCE) but need separate layouts: the irradiance
    // atlas is rgba16f and the distance atlas rg16f.
    class Blend_pass
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout>        bind_group_layout;
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages> shader_stages;
        std::unique_ptr<erhe::graphics::Compute_pipeline>         pipeline;
    };

    // (Re)creates the probe textures for the current grid + texel settings.
    void allocate_textures(erhe::graphics::Command_buffer& command_buffer);

    // Refits + reallocates when needed. Returns false when there is no
    // usable volume this tick.
    [[nodiscard]] auto update_volume(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool;

    // Writes this tick's control UBO (grid, dispatch window, rotation).
    [[nodiscard]] auto update_control_buffer() -> erhe::graphics::Ring_buffer_range;

    // Builds one of the two blend passes from the shared ddgi_blend.comp.
    void create_blend_pass(
        erhe::graphics::Device& graphics_device,
        Blend_pass&             pass,
        bool                    distance,
        const char*             image_name,
        const char*             image_format,
        const char*             debug_label
    );

    // Builds the reference irradiance pipeline (ddgi_reference.comp) and its
    // persistent host-visible point / result buffers. Takes the probe
    // trace's ray-hit defines and extensions, so both compile the shared
    // hit path identically.
    void create_reference_pass(
        erhe::graphics::Device&                                     graphics_device,
        erhe::scene_renderer::Program_interface&                    program_interface,
        const std::vector<std::pair<std::string, std::string>>&     ray_hit_defines,
        const std::vector<erhe::graphics::Shader_stage_extension>& ray_hit_extensions
    );

    // True while a reference query has chunks left to record.
    [[nodiscard]] auto reference_needs_dispatch() const -> bool;
    void               fail_reference_query    (const char* reason);

    // Records the next chunk of the reference query against this frame's
    // trace inputs (the same TLAS, light block and material set the probe
    // trace uses).
    void record_reference_chunk(
        erhe::graphics::Command_buffer&          command_buffer,
        const Scene_tlas::Frame&                 tlas_frame,
        const erhe::graphics::Ring_buffer_range& light_range,
        erhe::scene_renderer::Material_set&      material_set
    );

    // Builds the irradiance query pipeline (ddgi_sample.comp) and its
    // persistent host-visible input / output buffers.
    void create_query_pass(
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface
    );

    // Copies the probe data texture into a host-visible buffer: the mirror
    // the debug overlay reads, or the probe state readback. Recorded into the
    // frame's command buffer, so the overlay sees the previous frame's
    // probes - fine for a debug aid, and it costs no stall.
    void copy_probe_data(erhe::graphics::Command_buffer& command_buffer, erhe::graphics::Buffer& destination);

    // Per-pass GPU timer plus a fixed ring of its recent results.
    class Pass_timing
    {
    public:
        std::unique_ptr<erhe::graphics::Gpu_timer>     timer;
        std::array<uint64_t, c_timing_history_size>    history_ns{};
        std::size_t                                    history_count{0};
        std::size_t                                    history_next {0};
        uint64_t                                       last_ns      {0};
    };

    // Takes each pass timer's latest result into its history. Called once per
    // update, before the update records its own timestamps.
    void sample_pass_timings();
    void clear_pass_timings ();

    // A uniformly distributed random rotation for this tick's ray set.
    [[nodiscard]] auto next_random_rotation() -> glm::vec4;

    erhe::graphics::Device& m_graphics_device;
    App_context&            m_context;
    // Live reference to the editor's Ddgi_config (editor_settings.ddgi).
    const Ddgi_config&      m_config;
    Producer_selection      m_selection{Producer_selection::deselected};
    bool                    m_supported{false};

    Grid m_grid{};
    int  m_rays_per_probe   {0};
    int  m_irradiance_texels{0};
    int  m_distance_texels  {0};
    int  m_probes_per_update{0};
    // Probe tiles per atlas row (get_probe_field_tiles_per_row()).
    int  m_tiles_per_row    {0};
    // Round-robin cursor: the first probe this tick's budget updates.
    uint32_t m_probe_cursor{0};
    // Config values the current grid was fitted with. Changing any of them
    // changes the fit itself, so they force a refit even when the scene's
    // content bounds did not move.
    float m_fit_spacing_m {0.0f};
    float m_fit_padding_m {-1.0f};
    int   m_fit_max_probes{0};

    std::shared_ptr<erhe::graphics::Texture> m_irradiance_texture;
    std::shared_ptr<erhe::graphics::Texture> m_distance_texture;
    std::shared_ptr<erhe::graphics::Texture> m_probe_data_texture;
    std::shared_ptr<erhe::graphics::Texture> m_ray_data_texture;
    std::size_t                              m_texture_byte_count{0};

    // The padded box the current grid was fitted to, and the refit rule.
    Probe_volume_bounds m_volume_bounds{};

    // Radiance a probe ray gets when it escapes the scene. Scene ambient for
    // now; the atmosphere LUTs are a later refinement.
    glm::vec3 m_sky_radiance{0.0f};

    // GPU side. The acceleration structures are the shared Scene_tlas; the
    // material / light buffers and the texture heap mirror
    // Ray_trace_renderer's, because the probe trace shades hits with the
    // same erhe_ray_hit.glsl path.
    std::unique_ptr<Scene_tlas>                               m_scene_tlas;
    erhe::graphics::Shader_resource                           m_control_block;
    std::unique_ptr<erhe::graphics::Ring_buffer_client>       m_control_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_trace_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_trace_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_trace_pipeline;

    Blend_pass m_blend_irradiance;
    Blend_pass m_blend_distance;

    // Probe relocation + classification (phase 5). Shares the blend passes'
    // shape: control UBO + ray data + the probe data texture it writes.
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_relocate_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_relocate_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_relocate_pipeline;
    // No material buffer, texture heap or fallback pair: the tracing dispatch
    // binds the scene root's forward Material_set, which owns all three
    // (doc/erhe/draw_list_material_set.md D5).
    std::unique_ptr<erhe::scene_renderer::Light_buffer>       m_light_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_projections>  m_light_projections;
    uint32_t                                                  m_tlas_binding_point      {0};
    uint32_t                                                  m_ray_data_binding_point  {0};
    uint32_t                                                  m_probe_data_binding_point{0};

    // Control block field offsets, resolved once at construction.
    class Control_offsets
    {
    public:
        std::size_t grid_origin    {0};
        std::size_t grid_spacing   {0};
        std::size_t grid_counts    {0};
        std::size_t dispatch       {0};
        std::size_t random_rotation{0};
        std::size_t params         {0};
        std::size_t sky_radiance   {0};
        std::size_t flags          {0};
        std::size_t atlas          {0};
        std::size_t history        {0};
    };
    Control_offsets m_control_offsets{};

    // Temporal history of the probes (the blend hysteresis): reset on every
    // allocation and by the change messages (doc/editor/ddgi.md "History
    // reset").
    Temporal_history                                                m_history{};
    erhe::message_bus::Subscription<Mesh_geometry_changed_message>  m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Items_removed_message>          m_items_removed_subscription;
    erhe::message_bus::Subscription<Scene_lighting_changed_message> m_scene_lighting_changed_subscription;
    // The probe trace samples the previous field at hits (bounces multi):
    // user binding points of the irradiance and distance atlas samplers.
    uint32_t                                                        m_trace_irradiance_binding_point{0};
    uint32_t                                                        m_trace_distance_binding_point  {0};

    std::mt19937 m_random_engine{0x0DD91u};

    // Host-visible mirror of the probe data texture (xyz relocation offset,
    // w state), refreshed while the probe overlay is enabled.
    std::unique_ptr<erhe::graphics::Buffer> m_probe_readback_buffer;
    bool                                    m_probe_readback_valid{false};

    // Probe state readback (request_probe_states()): a separate host-visible
    // copy, so the overlay's per-frame copies never overwrite one being read.
    // The snapshot vector is filled on the MCP path only.
    std::unique_ptr<erhe::graphics::Buffer> m_probe_state_readback_buffer;
    bool                                    m_probe_state_requested        {false};
    bool                                    m_probe_state_in_flight        {false};
    bool                                    m_probe_state_valid            {false};
    uint64_t                                m_probe_state_frame            {0};
    uint64_t                                m_probe_state_copy_update_count{0};
    std::vector<glm::vec4>                  m_probe_states;
    Probe_state_summary                     m_probe_state_summary;

    std::array<Pass_timing, c_ddgi_pass_count> m_pass_timings;
    uint64_t                                   m_update_count       {0};
    uint64_t                                   m_timing_sample_count{0};

    // Irradiance query (ddgi_sample.comp). The input and output buffers are
    // persistent, host-visible and sized for c_max_irradiance_query_points;
    // the point / result vectors are filled on the MCP path, never per frame.
    erhe::graphics::Shader_resource                           m_query_input_block;
    erhe::graphics::Shader_resource                           m_query_output_block;
    std::size_t                                               m_query_header_offset{0};
    std::size_t                                               m_query_points_offset{0};
    std::size_t                                               m_query_output_offset{0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_query_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_query_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_query_pipeline;
    std::unique_ptr<erhe::graphics::Buffer>                   m_query_input_buffer;
    std::unique_ptr<erhe::graphics::Buffer>                   m_query_output_buffer;
    const erhe::graphics::Sampler*                            m_ddgi_sampler{nullptr};
    std::vector<Irradiance_query_point>                       m_query_points;
    std::vector<glm::vec3>                                    m_query_results;
    Irradiance_query_state                                    m_query_state       {Irradiance_query_state::idle};
    uint64_t                                                  m_query_frame       {0};
    uint64_t                                                  m_query_update_count{0};

    // Reference irradiance query (ddgi_reference.comp). Buffers persistent
    // and host-visible, sized for c_max_irradiance_query_points; the point /
    // result vectors are filled on the MCP path only.
    erhe::graphics::Shader_resource                           m_reference_control_block;
    erhe::graphics::Shader_resource                           m_reference_input_block;
    erhe::graphics::Shader_resource                           m_reference_output_block;
    std::size_t                                               m_reference_dispatch_offset{0};
    std::size_t                                               m_reference_params_offset  {0};
    std::size_t                                               m_reference_sky_offset     {0};
    std::size_t                                               m_reference_points_offset  {0};
    std::size_t                                               m_reference_output_offset  {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_reference_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_reference_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_reference_pipeline;
    std::unique_ptr<erhe::graphics::Buffer>                   m_reference_input_buffer;
    std::unique_ptr<erhe::graphics::Buffer>                   m_reference_output_buffer;
    std::vector<Irradiance_query_point>                       m_reference_points;
    std::vector<Reference_irradiance_sample>                  m_reference_results;
    Reference_query_settings                                  m_reference_settings{};
    std::string                                               m_reference_error;
    Irradiance_query_state                                    m_reference_state     {Irradiance_query_state::idle};
    std::size_t                                               m_reference_next_point{0}; // first point of the next chunk
    uint64_t                                                  m_reference_frame     {0}; // frame of the last chunk
    float                                                     m_reference_intensity {1.0f};
};

} // namespace editor
