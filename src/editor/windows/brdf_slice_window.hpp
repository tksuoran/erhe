#pragma once

#include "erhe/application/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/windows/framebuffer_window.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/scene/viewport.hpp"

#include <memory>

namespace erhe::primitive
{
class Material;
}

namespace editor
{

class Brdf_slice_window;
class Brdf_slice_rendergraph_node;

class Brdf_slice_rendergraph_node
    : public erhe::application::Texture_rendergraph_node
{
public:
    Brdf_slice_rendergraph_node();

    // Implements erhe::application::Rendergraph_node
    void execute_rendergraph_node() override;

    void set_area_size(int size);

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

private:
    void initialize_pipeline();

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    erhe::renderer::Pipeline_renderpass                 m_renderpass;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    int                                                 m_area_size{0};
};


/// Rendergraph sink node for showing texture in ImGui window
class Brdf_slice_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Brdf_slice_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});
    static constexpr std::string_view c_title{"BRDF Slice"};

    Brdf_slice_window ();
    ~Brdf_slice_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Overrides Framebuffer_window / Imgui_window
    void imgui() override;

    float                                      phi         {0.0f};
    float                                      incident_phi{0.0f};
    std::shared_ptr<erhe::primitive::Material> material;

private:
    std::shared_ptr<Brdf_slice_rendergraph_node> m_node;
};

extern Brdf_slice_window* g_brdf_slice_window;

} // namespace editor
