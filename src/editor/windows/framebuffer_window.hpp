#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
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

namespace editor
{

class Editor_imgui_windows;
class Gl_context_provider;

class Framebuffer_window
    : public Imgui_window
{
public:
    Framebuffer_window(
        const std::string_view debug_label,
        const std::string_view title
    );

    void initialize(
        Editor_imgui_windows&                       editor_imgui_windows,
        const std::shared_ptr<Gl_context_provider>& context_provider,
        erhe::graphics::Shader_stages*              shader_stages
    );

    // Implements Imgui_window
    void imgui() override;

    virtual auto get_size      () const -> glm::vec2;
    virtual void bind_resources();

    // Public API
    void render            (erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker);
    void update_framebuffer();

private:
    std::string                                         m_debug_label;
    erhe::scene::Viewport                               m_viewport{0, 0, 0, 0, true};
    erhe::graphics::Vertex_attribute_mappings           m_empty_attribute_mappings;
    erhe::graphics::Vertex_format                       m_empty_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    erhe::graphics::Pipeline                            m_pipeline;
    std::shared_ptr<erhe::graphics::Texture>            m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>        m_framebuffer;
};

} // namespace editor
