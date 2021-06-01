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
    class Mesh;
    class Node;
}

namespace editor
{

class Programs;
class Mesh_memory;
class Shadow_renderer;

class Forward_renderer
    : public erhe::components::Component,
      public Base_renderer
{
public:
    enum class Pass : unsigned int
    {
        polygon_fill = 0,
        edge_lines,
        polygon_centroids,
        corner_points,
        tag_depth_hidden_with_stencil,  // uses stencil value 1
        tag_depth_visible_with_stencil, // uses stencil value 2
        clear_depth,
        depth_only,
        require_stencil_tag_depth_visible,            // uses stencil value 2
        require_stencil_tag_depth_hidden_and_blend,   // uses stencil value 1
        hidden_line_with_blend
    };

    static constexpr const char* c_pass_strings[] =
    {
        "Polygon Fill",
        "Edge Lines",
        "Polygon Centroids",
        "Corner Points",
        "Tag Depth Hidden with Stencil 1",
        "Tag Depth Visible with Stencil 2",
        "Clear Depth",
        "Depth Only",
        "Require Stencil Tag 2 (Depth Visible) Color",
        "Require Stencil Tag 1 (Depth Hidden) Color with Blend",
        "Hidden Line with Blend",
    };

    Forward_renderer();

    virtual ~Forward_renderer() = default;

    // Implements Component
    void connect() override;
    void initialize_component() override;

    void render(erhe::scene::Viewport       viewport,
                erhe::scene::ICamera&       camera,
                Layer_collection&           layers,
                const Material_collection&  materials,
                std::initializer_list<Pass> passes,
                uint64_t                    visibility_mask);

private:
    auto select_pipeline      (Pass pass) const -> const erhe::graphics::Pipeline*;
    auto select_primitive_mode(Pass pass) const -> erhe::primitive::Primitive_mode;

    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Programs>                             m_programs;

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

    erhe::graphics::Pipeline                              m_pipeline_edge_lines;
    erhe::graphics::Pipeline                              m_pipeline_points;
    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_vertex_input;
};

} // namespace editor
