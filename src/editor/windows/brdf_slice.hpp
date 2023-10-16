#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_math/viewport.hpp"

#include <memory>

namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
}

namespace editor
{

class Brdf_slice;
class Editor_context;
class Programs;

class Brdf_slice_rendergraph_node
    : public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Brdf_slice_rendergraph_node(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Brdf_slice&                             brdf_slice,
        Programs&                               programs
    );

    // Implements erhe::rendergraph::Rendergraph_node
    void execute_rendergraph_node() override;

    void set_area_size(int size);

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::math::Viewport override;

    // Public API
    void set_material(const std::shared_ptr<erhe::primitive::Material>& material);
    [[nodiscard]] auto get_material() const -> erhe::primitive::Material*;

private:
    erhe::scene_renderer::Forward_renderer&    m_forward_renderer;
    Brdf_slice&                                m_brdf_slice;
    std::shared_ptr<erhe::primitive::Material> m_material;

    erhe::graphics::Vertex_input_state  m_empty_vertex_input;
    erhe::renderer::Pipeline_renderpass m_renderpass;
    erhe::graphics::Vertex_input_state  m_vertex_input;
    int                                 m_area_size{0};
};


/// Rendergraph sink node for showing texture in ImGui window
class Brdf_slice
{
public:
    Brdf_slice(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Editor_context&                         editor_context,
        Programs&                               programs
    );

    auto get_node() const -> Brdf_slice_rendergraph_node*;
    void show_brdf_slice(int area_size);

    float phi         {0.0f};
    float incident_phi{0.0f};

private:
    erhe::rendergraph::Rendergraph&              m_rendergraph;
    Editor_context&                              m_context;
    std::shared_ptr<Brdf_slice_rendergraph_node> m_node;
};

} // namespace editor
