#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"

#include <memory>

namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::primitive      { class Material; }
namespace erhe::rendergraph    { class Rendergraph; }
namespace erhe::scene_renderer { class Forward_renderer; }

namespace editor {

class Brdf_slice;
class App_context;
class Programs;

class Brdf_slice_rendergraph_node : public erhe::rendergraph::Texture_rendergraph_node
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

    // Public API
    void set_material(const std::shared_ptr<erhe::primitive::Material>& material);
    [[nodiscard]] auto get_material() const -> erhe::primitive::Material*;

private:
    erhe::scene_renderer::Forward_renderer&    m_forward_renderer;
    Brdf_slice&                                m_brdf_slice;
    std::shared_ptr<erhe::primitive::Material> m_material;

    erhe::graphics::Vertex_input_state m_empty_vertex_input;
    erhe::renderer::Pipeline_pass      m_pipeline_pass;
    int                                m_area_size{0};
};


/// Rendergraph sink node for showing texture in ImGui window
class Brdf_slice
{
public:
    Brdf_slice(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        App_context&                            app_context,
        Programs&                               programs
    );

    auto get_node() const -> Brdf_slice_rendergraph_node*;
    void show_brdf_slice(int area_size);

    float phi         {0.0f};
    float incident_phi{0.0f};

private:
    erhe::rendergraph::Rendergraph&              m_rendergraph;
    App_context&                                 m_context;
    std::shared_ptr<Brdf_slice_rendergraph_node> m_node;
};

}
