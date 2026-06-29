#pragma once

#include "renderers/composer.hpp"
#include "renderers/composition_pass.hpp" // TODO remove - for Fill_mode, Blend_mode, Selection_mode
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>

namespace erhe::commands       { class Commands; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::scene_renderer { class Content_wide_line_renderer; class Mesh_memory; }
namespace erhe::window         { class Context_window; }

struct Graphics_preset_entry;

namespace editor {

class App_context;
class App_message_bus;
class App_rendering;
class App_settings;
class Programs;
class Render_context;
class Renderable;
class Scene_view;
class Shadow_render_node;

class Capture_frame_command : public erhe::commands::Command
{
public:
    Capture_frame_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Pipeline_renderpasses
{
public:
    // reverse_depth is the static device value (Device::get_reverse_depth()),
    // queried once by the owner and passed here so the init-list can bake the
    // depth state. There is no runtime rebuild -- reverse-Z never changes while
    // the editor runs.
    Pipeline_renderpasses(
        erhe::graphics::Device&            graphics_device,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Programs&                          programs,
        bool                               reverse_depth
    );

    bool                                 m_y_flip;
    erhe::graphics::Vertex_input_state   m_empty_vertex_input;
    // Mirrored (negative determinant) meshes are handled per bucket by
    // Forward_renderer requesting the front-face-flipped pipeline variant
    // (Base_render_pipeline::get_pipeline_for front_face_flip), so a single
    // base pipeline per selection state suffices.
    erhe::graphics::Base_render_pipeline polygon_fill_standard;
    erhe::graphics::Base_render_pipeline polygon_fill_standard_selected;
    // Solid-wireframe overlay: triangle fill, depth less-or-equal, no depth
    // write, no stencil -- draws the expanded fill (with the wireframe variant)
    // at equal depth over the normal polygon fill.
    erhe::graphics::Base_render_pipeline solid_wireframe;
    erhe::graphics::Color_blend_state    line_hidden_blend_state;
    erhe::graphics::Base_render_pipeline line_hidden_blend;
    erhe::graphics::Base_render_pipeline brush_back;
    erhe::graphics::Base_render_pipeline brush_front;
    erhe::graphics::Base_render_pipeline edge_lines;
    erhe::graphics::Base_render_pipeline outline;
    erhe::graphics::Base_render_pipeline corner_points;
    erhe::graphics::Base_render_pipeline polygon_centroids;
    //erhe::graphics::Base_render_pipeline rendertarget_meshes;
    erhe::graphics::Base_render_pipeline sky;
    erhe::graphics::Base_render_pipeline grid;
};

class App_rendering
{
public:
    App_rendering(
        erhe::commands::Commands&          commands,
        erhe::graphics::Device&            graphics_device,
        App_context&                       context,
        App_message_bus&                   app_message_bus,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Programs&                          programs
    );

    [[nodiscard]] auto create_shadow_node_for_scene_view(
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_settings&                   app_settings,
        Scene_view&                     scene_view
    ) -> std::shared_ptr<Shadow_render_node>;

    [[nodiscard]] auto get_shadow_node_for_view(const Scene_view& scene_view) -> std::shared_ptr<Shadow_render_node>;
    [[nodiscard]] auto get_all_shadow_nodes    () -> const std::vector<std::shared_ptr<Shadow_render_node>>&;
    [[nodiscard]] auto is_capturing            () const -> bool;

    // Drop the Shadow_render_node from m_all_shadow_render_nodes. Caller
    // is expected to be Scene_builder_viewport_resources_operation's undo
    // path tearing down a Viewport_scene_view: the only other shared_ptr
    // owner is that operation, so dropping ours lets the node's destructor
    // unregister it from the Rendergraph. Returns true if a matching entry
    // was found and erased.
    auto destroy_shadow_node(const std::shared_ptr<Shadow_render_node>& shadow_render_node) -> bool;

    void trigger_capture            ();
    // include_overlay=false renders only content passes (the overlay -- tool /
    // rendertarget meshes -- is then drawn after post-processing by
    // render_overlay). When post-processing is disabled, pass true to render
    // content + overlay in the same pass. See issue #230.
    void render_viewport_main       (const Render_context& context, bool include_overlay);
    void render_overlay             (const Render_context& context);
    void render_viewport_renderables(const Render_context& context);
    // Renders the selected composition-pass phases (content and/or overlay).
    // The VR path renders both in one pass (no post-processing).
    void render_composer            (const Render_context& context, bool include_content, bool include_overlay);
    void render_id                  (const Render_context& context);
    void process_start_capture      ();
    void process_end_capture        ();
    void set_grid_visibility        (bool visible);
    void set_grid_label             (const glm::vec4& grid_label);
    void set_grid_colors            (const std::array<glm::vec4, 4>& level_colors, const glm::vec4& label_color);
    void set_grid_line_widths       (const glm::vec4& level_widths);
    void set_grid_sizes             (const glm::vec4& level_cell_sizes);
    // Refreshes the sky composition pass parameters from
    // Editor_settings_config::sky (edited in the Settings window).
    void update_sky_parameters      ();

    void add   (Renderable* renderable);
    void remove(Renderable* renderable);

    auto make_composition_pass(std::string_view name) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view        name,
        Composition_pass_data&& data,
        bool                    selected
    ) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view                                             name,
        Composition_pass_data&&                                      data,
        std::initializer_list<erhe::graphics::Base_render_pipeline*> pipelines
    ) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view                           name,
        const std::shared_ptr<Composition_pass>&   base_pass,
        erhe::scene_renderer::Blending_mode_policy blending_mode_policy
    ) -> std::shared_ptr<Composition_pass>;

    // Read-only access to the Composer's pass list for callers that need
    // to walk the variant-aware Base_render_pipeline pointers. Returns
    // the underlying vector by const reference WITHOUT taking the
    // Composer's mutex, so callers must guarantee single-threaded access
    // at the point of call. Intended exclusively for init-time use (the
    // shader-variant prewarm in renderers/prewarm.cpp); do not call from
    // the render thread.
    [[nodiscard]] auto composition_passes() const -> const std::vector<std::shared_ptr<Composition_pass>>&;

    void imgui();
    void request_renderdoc_capture();

    glm::uvec4                        debug_joint_indices{0, 0, 0, 0};
    std::vector<glm::vec4>            debug_joint_colors;
    std::shared_ptr<Composition_pass> selection_outline;
    std::shared_ptr<Composition_pass> hover_outline;
    std::shared_ptr<Composition_pass> edge_lines_not_selected;
    std::shared_ptr<Composition_pass> edge_lines_selected;
    std::shared_ptr<Composition_pass> translucent_outline;
    // Overlay pass that draws rendertarget meshes (e.g. the hotbar), ignoring
    // camera exposure and rendered after post-processing when enabled (#230).
    std::shared_ptr<Composition_pass> rendertarget;

private:
    void handle_graphics_settings_changed(Graphics_preset_entry* graphics_preset);

    [[nodiscard]] auto get_render_pipeline_state(
        const Composition_pass& renderpass,
        bool                    selected
    ) -> erhe::graphics::Base_render_pipeline*;

    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    erhe::message_bus::Subscription<Graphics_settings_message> m_graphics_settings_subscription;
    App_context&              m_context;

    // Commands
    Capture_frame_command     m_capture_frame_command;

    Pipeline_renderpasses             m_pipeline_passes;
    Composer                          m_composer;
    std::shared_ptr<Composition_pass> m_grid_composition_pass;
    std::shared_ptr<Composition_pass> m_sky_composition_pass;

    // TODO Re-add per-render-pass GPU timers when the composer's pipeline
    // passes own their own Render_pass objects.

    bool                      m_trigger_capture{false};
    std::vector<std::shared_ptr<Shadow_render_node>> m_all_shadow_render_nodes;

    ERHE_PROFILE_MUTEX(std::mutex, m_renderables_mutex);
    std::vector<Renderable*>       m_renderables;
};

static constexpr unsigned int s_stencil_edge_lines               =  1u; // 0 inc
static constexpr unsigned int s_stencil_tool_mesh_hidden         =  2u;
static constexpr unsigned int s_stencil_tool_mesh_visible        =  3u;

static constexpr unsigned int s_stencil_line_renderer_grid_minor =  8u;
static constexpr unsigned int s_stencil_line_renderer_grid_major =  9u;
static constexpr unsigned int s_stencil_line_renderer_selection  = 10u;
static constexpr unsigned int s_stencil_line_renderer_tools      = 11u;

}
