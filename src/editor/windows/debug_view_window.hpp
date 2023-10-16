#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_math/viewport.hpp"

namespace erhe::graphics {
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
}

namespace editor
{

class Debug_view_window;
class Depth_to_color_rendergraph_node;
class Editor_context;
class Editor_rendering;
class Mesh_memory;
class Programs;
class Shadow_render_node;

// Rendergraph processor node for converting depth texture into color texture.
class Depth_to_color_rendergraph_node
    : public erhe::rendergraph::Texture_rendergraph_node
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
    erhe::graphics::Vertex_input_state  m_empty_vertex_input;
    erhe::renderer::Pipeline_renderpass m_renderpass;
    int                                 m_light_index{};
};

class Debug_view_node
    : public erhe::rendergraph::Rendergraph_node
{
public:
    explicit Debug_view_node(erhe::rendergraph::Rendergraph& rendergraph);

    // Implements Rendergraph_node
    [[nodiscard]] auto get_type_name() const -> std::string_view override { return "Debug_view_node"; }
    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::math::Viewport override;

    void set_area_size(int size);

private:
    int m_area_size{0};
};

/// Rendergraph sink node for showing texture in ImGui window
class Debug_view_window
    : public erhe::imgui::Imgui_window
{
public:
    Debug_view_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Editor_context&                         editor_context,
        Editor_rendering&                       editor_rendering,
        Mesh_memory&                            mesh_memory,
        Programs&                               programs
    );

    // Overrides Framebuffer_window / Imgui_window
    void imgui () override;
    void hidden() override;

    // Public API
    //// void register_node(const std::shared_ptr<Shadow_render_node>& shadow_renderer_node)

private:
    void set_shadow_renderer_node(Shadow_render_node* node);

    Editor_context&                 m_context;
    Depth_to_color_rendergraph_node m_depth_to_color_node;
    Debug_view_node                 m_node;
    Shadow_render_node*             m_shadow_renderer_node{nullptr};
    int                             m_selected_node{0};
};

} // namespace editor
