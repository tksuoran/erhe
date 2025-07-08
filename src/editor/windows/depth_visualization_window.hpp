#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_math/viewport.hpp"

namespace erhe::graphics       { class Device; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::rendergraph    { class Rendergraph; }
namespace erhe::scene_renderer { class Forward_renderer; }

namespace editor {

class Depth_visualization_window;
class Depth_to_color_rendergraph_node;
class App_context;
class App_rendering;
class Mesh_memory;
class Programs;
class Shadow_render_node;

// Rendergraph processor node for converting depth texture into color texture.
class Depth_to_color_rendergraph_node : public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Depth_to_color_rendergraph_node(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Mesh_memory&                            mesh_memory,
        Programs&                               programs
    );

    // Implements erhe::rendergraph::Rendergraph_node
    void execute_rendergraph_node() override;

    // Public API
    [[nodiscard]] auto get_light_index() -> int&;

private:
    erhe::scene_renderer::Forward_renderer& m_forward_renderer;
    Mesh_memory&                            m_mesh_memory;

    // TODO These resources should not be per node
    erhe::graphics::Vertex_input_state m_empty_vertex_input;
    erhe::renderer::Pipeline_pass      m_pipeline_pass;
    int                                m_light_index{};
};

/// Rendergraph sink node for showing texture in ImGui window
class Depth_visualization_window : public erhe::imgui::Imgui_window
{
public:
    Depth_visualization_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        App_context&                            context,
        App_rendering&                          app_rendering,
        Mesh_memory&                            mesh_memory,
        Programs&                               programs
    );

    // Overrides Imgui_window
    void imgui () override;
    void hidden() override;

private:
    void set_shadow_renderer_node(const std::shared_ptr<Shadow_render_node>& shadow_node);

    App_context&                                  m_context;
    std::unique_ptr<Depth_to_color_rendergraph_node> m_depth_to_color_node{};
    std::weak_ptr<Shadow_render_node>                m_shadow_renderer_node{};
    int                                              m_selected_shadow_node{0};
};

}
