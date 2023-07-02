#pragma once

#include "erhe/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/rendergraph/rendergraph_node.hpp"
#include "erhe/imgui/windows/framebuffer_window.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/toolkit/viewport.hpp"

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

class Brdf_slice_window;
class Content_library_window;
class Programs;

class Brdf_slice_rendergraph_node
    : public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Brdf_slice_rendergraph_node(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Brdf_slice_window&                      brdf_slice_window,
        Content_library_window&                 content_library_window,
        Programs&                               programs
    );

    // Implements erhe::rendergraph::Rendergraph_node
    void execute_rendergraph_node() override;

    void set_area_size(int size);

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::toolkit::Viewport override;

private:
    erhe::scene_renderer::Forward_renderer& m_forward_renderer;
    Brdf_slice_window&                      m_brdf_slice_window;
    Content_library_window&                 m_content_library_window;

    erhe::graphics::Vertex_input_state  m_empty_vertex_input;
    erhe::renderer::Pipeline_renderpass m_renderpass;
    erhe::graphics::Vertex_input_state  m_vertex_input;
    int                                 m_area_size{0};
};


/// Rendergraph sink node for showing texture in ImGui window
class Brdf_slice_window
    : public erhe::imgui::Imgui_window
{
public:
    Brdf_slice_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Content_library_window&                 content_library_window,
        Programs&                               programs
    );

    // Overrides Framebuffer_window / Imgui_window
    void imgui() override;

    float                                      phi         {0.0f};
    float                                      incident_phi{0.0f};
    std::shared_ptr<erhe::primitive::Material> material;

private:
    erhe::rendergraph::Rendergraph& m_rendergraph;
    Content_library_window&         m_content_library_window;

    std::shared_ptr<Brdf_slice_rendergraph_node> m_node;
};

} // namespace editor
