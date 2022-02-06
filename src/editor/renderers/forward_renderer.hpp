#pragma once

#include "renderers/base_renderer.hpp"
#include "renderers/renderpass.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/primitive/primitive.hpp"

#include <glm/glm.hpp>

#include <functional>
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
        gui,
        hidden_line_with_blend,
        polygon_centroids,
        polygon_fill,
        require_stencil_tag_depth_visible,            // uses stencil value 2
        require_stencil_tag_depth_hidden_and_blend,   // uses stencil value 1
        tag_depth_hidden_with_stencil,  // uses stencil value 1
        tag_depth_visible_with_stencil, // uses stencil value 2
    };

    static constexpr std::array<std::string_view, 14> c_pass_strings =
    {
        "Brush Back",
        "Brush front",
        "Clear Depth",
        "Corner Points",
        "Depth Only",
        "Edge Lines",
        "GUI",
        "Hidden Line with Blend",
        "Polygon Centroids",
        "Polygon Fill",
        "Require Stencil Tag 1 (Depth Hidden) Color with Blend",
        "Require Stencil Tag 2 (Depth Visible) Color",
        "Tag Depth Hidden with Stencil 1",
        "Tag Depth Visible with Stencil 2",
    };

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    static constexpr std::string_view c_name{"Forward_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Forward_renderer ();
    ~Forward_renderer() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    class Render_parameters
    {
    public:
        const erhe::scene::Viewport&                                   viewport;
        const erhe::scene::ICamera*                                    camera           {nullptr};
        const std::vector<const erhe::scene::Mesh_layer*>&             mesh_layers;
        const erhe::scene::Light_layer*                                light_layer;
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::vector<Renderpass*>                                 passes;
        const erhe::scene::Visibility_filter                           visibility_filter{};
    };

    void render(const Render_parameters& parameters);

    void render_fullscreen(const Render_parameters& parameters);

private:
    // Component dependencies
    std::shared_ptr<Configuration>                        m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Programs>                             m_programs;
};

} // namespace editor
