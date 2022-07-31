#pragma once

#include "renderers/renderpass.hpp"

#include "erhe/application/render_graph_node.hpp"
#include "erhe/application/windows/framebuffer_window.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/viewport.hpp"

#include <memory>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace editor
{

class Debug_view_window;
class Forward_renderer;
class Programs;
class Shadow_renderer;

class Debug_view_render_graph_node
    : public erhe::application::Render_graph_node
{
public:
    Debug_view_render_graph_node(
        const std::shared_ptr<Debug_view_window>& debug_view_window
    );

private:
    // Implements erhe::application::Render_graph_node
    void execute_render_graph_node() override;

    std::shared_ptr<Debug_view_window> m_debug_view_window;
};

class Debug_view_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Debug_view_window"};
    static constexpr std::string_view c_title{"Debug View"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Debug_view_window ();
    ~Debug_view_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    void update_framebuffer(const glm::vec2 available_size);

    // Overrides Framebuffer_window / Imgui_window
    void imgui() override;

    // Public API
    void render();

private:
    // Component dependencies
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;

    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_empty_vertex_input;
    Renderpass                                            m_renderpass;
    int                                                   m_light_index{};
    erhe::scene::Viewport                                 m_viewport{0, 0, 0, 0, true};

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    std::shared_ptr<erhe::graphics::Texture>            m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>        m_framebuffer;
};

} // namespace editor
