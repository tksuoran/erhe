#pragma once

#include "renderers/composer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/renderpass.hpp" // TODO remove - for Fill_mode, Blend_mode, Selection_mode
#include "erhe_commands/command.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::window {
    class Context_window;
}

namespace editor {

class Editor_context;
class Editor_message_bus;
class Editor_rendering;
class Editor_settings;
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
    Capture_frame_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Pipeline_renderpasses
{
public:
    Pipeline_renderpasses(erhe::graphics::Instance& graphics_instance, Mesh_memory& mesh_memory, Programs& programs);

    erhe::graphics::Vertex_input_state  m_empty_vertex_input;
    erhe::renderer::Pipeline_renderpass polygon_fill_standard_opaque;
    erhe::renderer::Pipeline_renderpass polygon_fill_standard_opaque_selected;
    erhe::renderer::Pipeline_renderpass polygon_fill_standard_translucent;
    erhe::renderer::Pipeline_renderpass line_hidden_blend;
    erhe::renderer::Pipeline_renderpass brush_back;
    erhe::renderer::Pipeline_renderpass brush_front;
    erhe::renderer::Pipeline_renderpass edge_lines;
    erhe::renderer::Pipeline_renderpass selection_outline;
    erhe::renderer::Pipeline_renderpass corner_points;
    erhe::renderer::Pipeline_renderpass polygon_centroids;
    erhe::renderer::Pipeline_renderpass rendertarget_meshes;
    erhe::renderer::Pipeline_renderpass sky;
    erhe::renderer::Pipeline_renderpass grid;
};

class Editor_rendering
{
public:
    Editor_rendering(
        erhe::commands::Commands& commands,
        erhe::graphics::Instance& graphics_instance,
        Editor_context&           editor_context,
        Editor_message_bus&       editor_message_bus,
        Mesh_memory&              mesh_memory,
        Programs&                 programs
    );

    [[nodiscard]] auto create_shadow_node_for_scene_view(
        erhe::graphics::Instance&       graphics_instance,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_settings&                editor_settings,
        Scene_view&                     scene_view
    ) -> std::shared_ptr<Shadow_render_node>;

    [[nodiscard]] auto get_shadow_node_for_view(const Scene_view& scene_view) -> std::shared_ptr<Shadow_render_node>;
    [[nodiscard]] auto get_all_shadow_nodes    () -> const std::vector<std::shared_ptr<Shadow_render_node>>&;

    void set_tool_scene_root        (Scene_root* tool_scene_root);
    void trigger_capture            ();
    void render_viewport_main       (const Render_context& context);
    void render_viewport_renderables(const Render_context& context);
    void render_composer            (const Render_context& context);
    void render_id                  (const Render_context& context);
    void begin_frame                ();
    void end_frame                  ();

    void add   (Renderable* renderable);
    void remove(Renderable* renderable);

    Scene_root* tool_scene_root{nullptr};

    auto make_renderpass(const std::string_view name) -> std::shared_ptr<Renderpass>;

    void imgui();
    void request_renderdoc_capture();

    glm::uvec4                  debug_joint_indices{0, 0, 0, 0};
    std::vector<glm::vec4>      debug_joint_colors;
    std::shared_ptr<Renderpass> selection_outline;

private:
    void handle_graphics_settings_changed(Graphics_preset* graphics_preset);

    [[nodiscard]] auto get_pipeline_renderpass(
        const Renderpass&          renderpass,
        erhe::renderer::Blend_mode blend_mode,
        bool                       selected
    ) -> erhe::renderer::Pipeline_renderpass*;

    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    Editor_context&           m_context;

    // Commands
    Capture_frame_command     m_capture_frame_command;

    Pipeline_renderpasses     m_pipeline_renderpasses;
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
