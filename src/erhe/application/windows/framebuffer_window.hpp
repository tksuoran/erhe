#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

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
    class Shader_stages;
    class Texture;
}

namespace erhe::application
{

class Imgui_windows;
class Gl_context_provider;

class Framebuffer_window
    : public Imgui_window
{
public:
    Framebuffer_window(
        const std::string_view title,
        const std::string_view label
    );

    void initialize(
        erhe::components::Components& components
    );

    // Implements Imgui_window
    void imgui() override;

    // Implements Framebuffer window
    virtual auto get_size(glm::vec2 available_size) const -> glm::vec2;

    // Public API
    virtual void update_framebuffer();

    void bind_framebuffer();

protected:
    std::string                                         m_debug_label;
    bool                                                m_is_hovered{false};
    erhe::scene::Viewport                               m_viewport{0, 0, 0, 0, true};
    erhe::graphics::Vertex_attribute_mappings           m_empty_attribute_mappings;
    erhe::graphics::Vertex_format                       m_empty_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    std::shared_ptr<erhe::graphics::Texture>            m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>        m_framebuffer;
};

} // namespace erhe::application
