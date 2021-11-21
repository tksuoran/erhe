#pragma once

#include "renderers/base_renderer.hpp"

#include "erhe/components/component.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/primitive/primitive.hpp"

#include <glm/glm.hpp>
#include <initializer_list>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class OpenGL_state_tracker;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Node;
    class Visibility_filter;
}

namespace editor
{

class Configuration;
class Programs;
class Mesh_memory;
class Shadow_renderer;

class Forward_renderer
    : public erhe::components::Component
    , public Base_renderer
{
public:
    enum class Pass : unsigned int
    {
        brush_back = 0,
        brush_front,
        clear_depth,
        corner_points,
        depth_only,
        edge_lines,
        hidden_line_with_blend,
        polygon_centroids,
        polygon_fill,
        require_stencil_tag_depth_visible,            // uses stencil value 2
        require_stencil_tag_depth_hidden_and_blend,   // uses stencil value 1
        tag_depth_hidden_with_stencil,  // uses stencil value 1
        tag_depth_visible_with_stencil, // uses stencil value 2
    };

    static constexpr std::array<std::string_view, 13> c_pass_strings =
    {
        "Brush Back",
        "Brush front",
        "Clear Depth",
        "Corner Points",
        "Depth Only",
        "Edge Lines",
        "Hidden Line with Blend",
        "Polygon Centroids",
        "Polygon Fill",
        "Require Stencil Tag 1 (Depth Hidden) Color with Blend",
        "Require Stencil Tag 2 (Depth Visible) Color",
        "Tag Depth Hidden with Stencil 1",
        "Tag Depth Visible with Stencil 2",
    };

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer *>;

    static constexpr std::string_view c_name{"Forward_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Forward_renderer ();
    ~Forward_renderer() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    void render(
        erhe::scene::Viewport                 viewport,
        erhe::scene::ICamera&                 camera,
        const Mesh_layer_collection&          mesh_layers,
        const erhe::scene::Light_layer&       light_layer,
        const Material_collection&            materials,
        const std::initializer_list<Pass>     passes,
        const erhe::scene::Visibility_filter& visibility_mask
    );

private:
    auto select_pipeline      (Pass pass) const -> const erhe::graphics::Pipeline*;
    auto select_primitive_mode(Pass pass) const -> erhe::primitive::Primitive_mode;

    Configuration*                        m_configuration         {nullptr};
    erhe::graphics::OpenGL_state_tracker* m_pipeline_state_tracker{nullptr};
    Mesh_memory*                          m_mesh_memory           {nullptr};
    Shadow_renderer*                      m_shadow_renderer       {nullptr};
    Programs*                             m_programs              {nullptr};

    erhe::graphics::Depth_stencil_state                   m_depth_stencil_tool_set_hidden;
    erhe::graphics::Depth_stencil_state                   m_depth_stencil_tool_set_visible;
    erhe::graphics::Depth_stencil_state                   m_depth_stencil_tool_test_for_hidden;
    erhe::graphics::Depth_stencil_state                   m_depth_stencil_tool_test_for_visible;
    erhe::graphics::Depth_stencil_state                   m_depth_hidden;
    erhe::graphics::Color_blend_state                     m_color_blend_constant_point_six;
    erhe::graphics::Color_blend_state                     m_color_blend_constant_point_two;

    erhe::graphics::Pipeline                              m_pipeline_fill;

    // Six passes for rendering tools that can be partially occluded
    erhe::graphics::Pipeline                              m_pipeline_tool_hidden_stencil_pass;
    erhe::graphics::Pipeline                              m_pipeline_tool_visible_stencil_pass;
    erhe::graphics::Pipeline                              m_pipeline_tool_depth_clear_pass;
    erhe::graphics::Pipeline                              m_pipeline_tool_depth_pass;
    erhe::graphics::Pipeline                              m_pipeline_tool_visible_color_pass;
    erhe::graphics::Pipeline                              m_pipeline_tool_hidden_color_pass;

    erhe::graphics::Pipeline                              m_pipeline_line_hidden_blend;

    erhe::graphics::Pipeline                              m_pipeline_brush_back;
    erhe::graphics::Pipeline                              m_pipeline_brush_front;

    erhe::graphics::Pipeline                              m_pipeline_edge_lines;
    erhe::graphics::Pipeline                              m_pipeline_points;
    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_vertex_input;
};

} // namespace editor
