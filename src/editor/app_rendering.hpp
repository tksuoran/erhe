#pragma once

#include "renderers/composer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/composition_pass.hpp" // TODO remove - for Fill_mode, Blend_mode, Selection_mode
#include "erhe_commands/command.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::commands { class Commands; }
namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::window   { class Context_window; }

namespace editor {

class App_context;
class App_message_bus;
class App_rendering;
class App_settings;
class Graphics_preset;
class Mesh_memory;
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
    Pipeline_renderpasses(erhe::graphics::Device& graphics_device, Mesh_memory& mesh_memory, Programs& programs);

    erhe::graphics::Vertex_input_state m_empty_vertex_input;
    erhe::renderer::Pipeline_pass      polygon_fill_standard_opaque;
    erhe::renderer::Pipeline_pass      polygon_fill_standard_opaque_selected;
    erhe::renderer::Pipeline_pass      polygon_fill_standard_translucent;
    erhe::renderer::Pipeline_pass      line_hidden_blend;
    erhe::renderer::Pipeline_pass      brush_back;
    erhe::renderer::Pipeline_pass      brush_front;
    erhe::renderer::Pipeline_pass      edge_lines;
    erhe::renderer::Pipeline_pass      selection_outline;
    erhe::renderer::Pipeline_pass      corner_points;
    erhe::renderer::Pipeline_pass      polygon_centroids;
    erhe::renderer::Pipeline_pass      rendertarget_meshes;
    erhe::renderer::Pipeline_pass      sky;
    erhe::renderer::Pipeline_pass      grid;
};

class App_rendering
{
public:
    App_rendering(
        erhe::commands::Commands& commands,
        erhe::graphics::Device&   graphics_device,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Mesh_memory&              mesh_memory,
        Programs&                 programs
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

    void trigger_capture            ();
    void render_viewport_main       (const Render_context& context);
    void render_viewport_renderables(const Render_context& context);
    void render_composer            (const Render_context& context);
    void render_id                  (const Render_context& context);
    void begin_frame                ();
    void end_frame                  ();

    void add   (Renderable* renderable);
    void remove(Renderable* renderable);

    auto make_composition_pass(const std::string_view name) -> std::shared_ptr<Composition_pass>;

    void imgui();
    void request_renderdoc_capture();

    glm::uvec4                        debug_joint_indices{0, 0, 0, 0};
    std::vector<glm::vec4>            debug_joint_colors;
    std::shared_ptr<Composition_pass> selection_outline;

private:
    void handle_graphics_settings_changed(Graphics_preset* graphics_preset);

    [[nodiscard]] auto get_pipeline_pass(
        const Composition_pass&    renderpass,
        erhe::renderer::Blend_mode blend_mode,
        bool                       selected
    ) -> erhe::renderer::Pipeline_pass*;

    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    App_context&              m_context;

    // Commands
    Capture_frame_command     m_capture_frame_command;

    Pipeline_renderpasses     m_pipeline_passes;
    Composer                  m_composer;

    erhe::graphics::Gpu_timer m_content_timer;
    erhe::graphics::Gpu_timer m_selection_timer;
    erhe::graphics::Gpu_timer m_gui_timer;
    erhe::graphics::Gpu_timer m_brush_timer;
    erhe::graphics::Gpu_timer m_tools_timer;

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
