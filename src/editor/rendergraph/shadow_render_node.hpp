#pragma once

#include "renderers/render_frame_results.hpp"

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/light_snapshot.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace erhe::dataformat     { enum class Format : unsigned int; }
namespace erhe::graphics       { class Command_buffer; class Device; class Gpu_timer; }
namespace erhe::scene          { class Camera; class Light; }
namespace erhe::scene_renderer { class Draw_list_scene; class Light_projections; class Render_bucket_snapshot; }

namespace editor {

class App_context;
class Scene_root;
class Scene_view;
class Viewport_scene_view;

// The 2D shadow map depth format for a requested depth bit count: the
// supported depth format nearest to the request, preferring more bits over
// fewer. Shadow_render_node::reconfigure() creates its shadow map texture in
// this format; the init-time prewarm uses it to predict the
// ERHE_SHADOW_DEPTH_BITS variant before any shadow node exists.
[[nodiscard]] auto choose_shadow_depth_format(erhe::graphics::Device& graphics_device, int requested_depth_bits) -> erhe::dataformat::Format;

// What Shadow_render_node::reconfigure() allocates the shadow maps and their
// render passes for. depth_bits is the requested depth bit count, the input
// to choose_shadow_depth_format().
class Shadow_node_configuration
{
public:
    [[nodiscard]] auto operator==(const Shadow_node_configuration& other) const -> bool = default;

    int  resolution        {0};
    int  light_count       {0};
    int  depth_bits        {0};
    bool distance_technique{false};
    int  point_resolution  {0};
    int  point_light_count {0};
};

// What the render phase of one frame produced for one Shadow_render_node,
// one per frame in flight (Render_frame_results): written by
// execute_rendergraph_node() into the slot of the frame it renders, read by
// the render phase of the same frame (the composition passes, the ray
// tracer, the depth visualization node) and by the main thread as the last
// rendered frame's (Debug_visualizations in M2, the MCP queries).
//
// Self-contained: Light_projection_transforms::light points into lights,
// and light_projections.parameters.view_camera / fit_settings at the copies
// below, never into the scene's light set or the view's camera snapshot,
// which the prepare phase of a later frame rewrites while the main thread
// still reads this result.
class Shadow_frame_result
{
public:
    erhe::scene_renderer::Light_projections      light_projections;
    std::vector<erhe::scene::Light_snapshot>     lights;
    erhe::scene::Camera_snapshot                 view_camera;
    erhe::scene::Shadow_frustum_fit_settings     fit_settings;
    // What the draw-list shadow path drew and culled per pass
    // (doc/erhe/draw_list_renderer.md Q6); empty on the bucket path and on
    // the frames that rendered no shadow maps.
    erhe::scene_renderer::Shadow_draw_statistics draw_statistics;
    // CPU wall time of this frame's pre_execute_rendergraph_node() and
    // execute_rendergraph_node() (P4 measurement,
    // doc/erhe/draw_list_renderer.md), and the running total and execute
    // count since construction up to and including this frame.
    double                                       cpu_time_us      {0.0};
    double                                       total_cpu_time_us{0.0};
    std::size_t                                  execute_count    {0};
};

// Helper rendergraph node calling Shadow_renderer

// TODO Think about Shadow_render_node / Shadow_renderer relationship,
//      should Shadow_render_node have more state?
class Shadow_render_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Shadow_render_node(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    context,
        Scene_view&                     scene_view,
        int                             resolution,
        int                             light_count,
        int                             depth_bits,
        int                             point_resolution,
        int                             point_light_count
    );
    ~Shadow_render_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Shadow_render_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;
    // Applies the frame's configuration, fits the light projections
    // (Shadow_renderer::fit()) and, on the draw-list path, adds the passes'
    // culling views before the frame's classification (doc/plans/culling.md
    // phase 6a); execute_rendergraph_node() draws the passes
    // (Shadow_renderer::draw()).
    void pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    auto get_producer_output_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto inputs_allowed() const -> bool override;

    // Public API

    // Main thread (the graphics preset change): the configuration the render
    // phase of the next prepared frame applies. The shadow technique is
    // taken from the render settings by prepare().
    void request_configuration(const Shadow_node_configuration& configuration);

    // Main thread, M2 (App_rendering::prepare_shadow_render_nodes): follows
    // the render settings copy's shadow technique (depth / distance) and
    // copies the requested configuration into this frame's slot. The render
    // phase of the frame applies it at the start of
    // pre_execute_rendergraph_node(), with the frame command buffer: the shadow
    // maps and render passes are created, replaced and dropped by the render
    // phase only, which is the only phase that reads them.
    void prepare();

    // Main thread, the last M2 step (App_rendering::prepare_composition, after
    // the material sets settled the forward set's slots): when this frame's
    // shadow pass takes the bucket path (use_draw_lists off), buckets the
    // casters of the view's content layer into the frame's slot
    // (Shadow_renderer::prepare, under item_host_mutex). The render phase
    // draws the slot and reads no Mesh or Node.
    void prepare_casters(uint64_t frame_index, std::size_t frames_in_flight);

    [[nodiscard]] auto get_scene_view       () -> Scene_view&;
    [[nodiscard]] auto get_scene_view       () const -> const Scene_view&;
    // Render phase: the light projections this frame's execute produced
    // (verified: the node executed in frame_index, which is the render
    // role's Device::get_frame_index()).
    [[nodiscard]] auto get_light_projections(uint64_t frame_index) const -> const erhe::scene_renderer::Light_projections&;
    // The result of frame_index: the main thread passes App_context::
    // last_rendered_frame_index (the last rendered frame), code that runs in
    // either phase App_context::get_results_frame_index(); null when the
    // node did not execute in that frame (or nothing was rendered yet).
    [[nodiscard]] auto get_frame_result     (std::optional<uint64_t> frame_index) const -> const Shadow_frame_result*;
    [[nodiscard]] auto get_texture          () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto get_viewport         () const -> erhe::math::Viewport;

    // Read-only access to the per-light shadow Render_pass list and the
    // active reverse-depth setting. Used by the init-time pipeline
    // prewarm (renderers/prewarm.cpp) to drive Shadow_renderer::
    // prewarm_pipelines so vkCreateGraphicsPipelines for each shadow
    // pass runs at init rather than first frame.
    [[nodiscard]] auto get_render_passes    () const -> std::span<const std::unique_ptr<erhe::graphics::Render_pass>>;
    [[nodiscard]] auto get_reverse_depth    () const -> bool;

    // Main thread: the CPU time totals of a frame result since the last
    // reset_cpu_time_stats(). The reset takes the totals of the given frame
    // (the last rendered one) as the new baseline; the running totals stay
    // the render phase's.
    [[nodiscard]] auto get_total_cpu_time_us(const Shadow_frame_result& result) const -> double;
    [[nodiscard]] auto get_execute_count    (const Shadow_frame_result& result) const -> std::size_t;
    void reset_cpu_time_stats(std::optional<uint64_t> frame_index);

private:
    // (Re)allocates the shadow maps and render passes for the configuration,
    // recording the layout transitions into the command buffer. The
    // constructor (before the node first executes) and the render phase
    // (apply_requested_configuration()) call it.
    void reconfigure(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer, const Shadow_node_configuration& configuration);
    // Render phase: applies the configuration the prepare phase of this frame
    // copied into its slot; a frame whose prepare phase did not run for this
    // node (it was created after that step) keeps the current configuration.
    void apply_requested_configuration(erhe::graphics::Command_buffer& command_buffer);

    // What pre_execute_rendergraph_node() decided for the frame's shadow
    // passes: the render parameters Shadow_renderer::fit() ran with, which
    // execute_rendergraph_node() of the same frame hands to
    // Shadow_renderer::draw(), so both see the same inputs (render phase
    // owned; the render phase renders one frame at a time).
    class Frame_passes
    {
    public:
        uint64_t                                                                frame_index{std::numeric_limits<uint64_t>::max()};
        // Keeps the frame's scene root (the parameters' joint poses,
        // material set and draw list scene) alive until execute drew.
        std::shared_ptr<Scene_root>                                             scene_root {};
        // Empty: the frame has no shadow passes (execute draws nothing).
        std::optional<erhe::scene_renderer::Shadow_renderer::Render_parameters> parameters{};
        std::array<uint64_t, 1>                                                 draw_list_layers{}; // the content layer id
    };
    Frame_passes                                              m_frame_passes;
    erhe::scene_renderer::Shadow_renderer::Cull_views         m_cull_views;
    // What the maps hold from earlier frames (render thread).
    erhe::scene_renderer::Shadow_map_cache                    m_map_cache;
    // Render_parameters::mesh_spans of the frame's parameters (the editor
    // hands Shadow_renderer no meshes: it reads the prepared bounds and
    // casters).
    std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>> m_no_mesh_spans; // default constructed: empty

    // Render phase owned: the running CPU time totals.
    double      m_total_cpu_time_us{0.0};
    std::size_t m_execute_count    {0};
    // Main thread owned: the totals reset_cpu_time_stats() took.
    double      m_total_cpu_time_baseline_us{0.0};
    std::size_t m_execute_count_baseline    {0};
    App_context&                                              m_context;
    Scene_view&                                               m_scene_view;
    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    // Shadow_technique_mode::distance R32F distance map (color attachment of the
    // render passes). Allocated only while the distance technique is active;
    // null otherwise (a full-resolution R32F array is large). The requested
    // depth bits are only the input to choose_shadow_depth_format(); the
    // shader variant follows the format of m_texture
    // (erhe::scene_renderer::get_shadow_depth_bits_axis()).
    std::shared_ptr<erhe::graphics::Texture>                  m_distance_texture;
    // The configuration last applied by reconfigure() (render phase owned
    // after construction).
    Shadow_node_configuration                                 m_configuration;
    // The configuration the main thread requests (main thread owned).
    Shadow_node_configuration                                 m_requested_configuration;
    // The requested configuration as the prepare phase of a frame copied it,
    // one slot per frame in flight (prepare()).
    class Configuration_slot
    {
    public:
        uint64_t                  frame_index  {std::numeric_limits<uint64_t>::max()};
        Shadow_node_configuration configuration{};
    };
    std::vector<Configuration_slot>                           m_configuration_slots;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_render_passes;

    // Omnidirectional point-light shadows: an R32F texture_cube_map_array (one
    // cube / 6 faces per shadow-casting point light) of radial distances, a
    // shared 2D depth scratch reused as the rasterization depth for every face,
    // and 6 * point_light_count render passes (face f of cube p ->
    // m_point_render_passes[6*p + f]). Sized by the preset's
    // point_shadow_resolution / point_shadow_light_count.
    std::shared_ptr<erhe::graphics::Texture>                  m_point_cube_texture;
    std::shared_ptr<erhe::graphics::Texture>                  m_point_cube_depth;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_point_render_passes;
    erhe::math::Viewport                                      m_point_viewport{0, 0, 0, 0};
    std::vector<std::string>                                  m_gpu_timer_labels;
    std::vector<std::unique_ptr<erhe::graphics::Gpu_timer>>   m_gpu_timers;
    erhe::math::Viewport                                      m_viewport{0, 0, 0, 0};
    // One result per frame in flight (the light projections, their light
    // and camera copies, the draw statistics and the CPU time).
    Render_frame_results<Shadow_frame_result>                 m_frame_results;
    // Render phase owned: the distance technique fallback report's state
    // (Light_projections::report_distance_fallbacks() compares with the
    // previous report), carried from frame result to frame result.
    std::vector<std::size_t>                                  m_distance_fallback_light_ids;
    // The bucket path's prepared casters, one slot per frame in flight
    // (prepare_casters()).
    class Caster_slot
    {
    public:
        uint64_t                                     frame_index{0};
        bool                                         prepared   {false};
        erhe::scene_renderer::Render_bucket_snapshot casters;
    };
    std::vector<Caster_slot>                                  m_caster_slots;

    // Diagnostics: log the fit view camera / projection / viewport only when
    // they change, so a run reveals which camera the shadow fit is fitted to
    // (the headset drives m_root_camera as perspective_xr from the combined
    // stereo eye frustum) and its fov, without per-frame log spam.
    // Headlight for a scene whose light layer is empty: one white directional
    // light along the view camera's axis, the way usdview lights a stage that
    // authors none (doc/erhe/usd_compatibility_design.md S1). It is not a scene item
    // - no hierarchy row, no save, no undo - and it lives here, per render
    // node, because its direction is the direction of THIS view's camera.
    // Its own resolved set is used in place of the scene's for that frame;
    // the scene's set stays untouched, so a scene shown in two viewports gets
    // one headlight per viewport. Render phase owned: the frame result keeps
    // its own copy of the lights it projected (publish_light_copies()).
    [[nodiscard]] auto resolve_headlight(const erhe::scene::Camera_snapshot& camera) -> erhe::scene_renderer::Light_set&;

    // Render phase, after the frame's projections were applied: copies the
    // light set's lights and the view camera into the frame result and
    // points its projections at the copies (see Shadow_frame_result).
    static void publish_light_copies(
        Shadow_frame_result&                   result,
        const erhe::scene_renderer::Light_set& light_set,
        const erhe::scene::Camera_snapshot&    camera
    );

    // Render phase, after publish_light_copies(): the frame's light array
    // and light cluster lists of this view (Light_buffer::write_view(),
    // doc/plans/clustered_lights.md C3, C4), binned for the view's render
    // camera (not the shadow fit override camera). Logs the cluster
    // overflow count each time it changes.
    void write_light_view(Shadow_frame_result& result);
    std::size_t                                               m_light_cluster_overflow_count{0};

    std::shared_ptr<erhe::scene::Light>                       m_headlight;
    std::array<std::shared_ptr<erhe::scene::Light>, 1>        m_headlight_lights;
    erhe::scene_renderer::Light_set                           m_headlight_set;
    glm::mat4                                                 m_headlight_world_from_node{1.0f};

    int         m_dbg_last_viewport_width {-1};
    int         m_dbg_last_viewport_height{-1};
    int         m_dbg_last_projection_type{-1};
};

}
