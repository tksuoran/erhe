#pragma once

#include "renderers/renderpass.hpp"

#include "erhe/application/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/windows/framebuffer_window.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/viewport.hpp"

#include <memory>


namespace erhe::application
{
    class Rendergraph;
}

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace editor
{

class Debug_view_window;
class Depth_to_color_rendergraph_node;
class Forward_renderer;
class Programs;
class Shadow_renderer;
class Shadow_render_node;

/// <summary>
/// Rendergraph processor node for converting depth texture into color texture.
/// </summary>
class Depth_to_color_rendergraph_node
    : public erhe::application::Texture_rendergraph_node
{
public:
    explicit Depth_to_color_rendergraph_node(
        erhe::components::Components& components
    );

    // Implements erhe::application::Rendergraph_node
    void execute_rendergraph_node() override;

    // Public API
    [[nodiscard]] auto get_light_index() -> int&;

private:
    void initialize_pipeline();

    std::shared_ptr<erhe::application::Gl_context_provider> m_gl_context_provider;
    std::shared_ptr<Forward_renderer>                       m_forward_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker>   m_pipeline_state_tracker;
    std::shared_ptr<Programs>                               m_programs;
    std::shared_ptr<Shadow_renderer>                        m_shadow_renderer;

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    Renderpass                                          m_renderpass;
    int                                                 m_light_index{};
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
};

/// <summary>
/// Rendergraph sink node for showing texture in ImGui window
/// </summary>
class Debug_view_window
    : public erhe::components::Component
    , public erhe::application::Rendergraph_node
    , public erhe::application::Imgui_window
    , public std::enable_shared_from_this<Debug_view_window>
{
public:
    static constexpr std::string_view c_type_name{"Viewport_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});
    static constexpr std::string_view c_title{"Debug View"};

    Debug_view_window ();
    ~Debug_view_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }
    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    // Overrides Framebuffer_window / Imgui_window
    void imgui () override;
    void hidden() override;

    // Public API
    //// void register_node(const std::shared_ptr<Shadow_render_node>& shadow_renderer_node)

private:
    void set_shadow_renderer_node(const std::shared_ptr<Shadow_render_node>& node);

    // Component dependencies
    std::shared_ptr<erhe::application::Rendergraph> m_rendergraph;
    std::shared_ptr<Shadow_renderer> m_shadow_renderer;

    std::shared_ptr<Depth_to_color_rendergraph_node> m_depth_to_color_node;
    std::shared_ptr<Shadow_render_node>              m_shadow_renderer_node;
    int m_area_size    {0};
    int m_selected_node{0};
};

} // namespace editor
