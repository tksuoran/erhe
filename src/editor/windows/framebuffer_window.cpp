#include "windows/framebuffer_window.hpp"
#include "editor_imgui_windows.hpp"

#include "graphics/gl_context_provider.hpp"

#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/texture.hpp"

#include <imgui.h>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Framebuffer_window::Framebuffer_window(
    const std::string_view debug_label,
    const std::string_view title
)
    : m_debug_label{debug_label}
    , Imgui_window {title}
{
}

void Framebuffer_window::initialize(
    Editor_imgui_windows&                       editor_imgui_windows,
    const std::shared_ptr<Gl_context_provider>& context_provider,
    erhe::graphics::Shader_stages*              shader_stages
)
{
    const Scoped_gl_context gl_context{context_provider};

    editor_imgui_windows.register_imgui_window(this);

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        m_empty_attribute_mappings,
        m_empty_vertex_format,
        nullptr,
        nullptr
    );

    m_pipeline = {
        .shader_stages  = shader_stages,
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = &erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = &erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = &erhe::graphics::Color_blend_state::color_blend_disabled
    };
}

auto Framebuffer_window::get_size() const -> glm::vec2
{
    return glm::vec2{256.0f, 256.0f};
}

void Framebuffer_window::bind_resources()
{
}

void Framebuffer_window::update_framebuffer()
{
    const auto win_min = ImGui::GetWindowContentRegionMin();
    const auto win_max = ImGui::GetWindowContentRegionMax();

    const ImVec2 win_size{
        win_max.x - win_min.x,
        win_max.y - win_min.y
    };

    const auto imgui_available_size = win_size;

    if (
        (imgui_available_size.x < 1) ||
        (imgui_available_size.y < 1)
    )
    {
        return;
    }

    const glm::vec2 available_size{
        static_cast<float>(imgui_available_size.x),
        static_cast<float>(imgui_available_size.y),
    };

    const glm::vec2 source_size = get_size();

    const float ratio_x   = available_size.x / source_size.x;
    const float ratio_y   = available_size.y / source_size.y;
    const float ratio_min = (std::min)(ratio_x, ratio_y);

    const glm::vec2  texture_size = source_size * ratio_min;
    const glm::ivec2 size{texture_size};

    if (
        m_texture &&
        (m_texture->width()  == size.x) &&
        (m_texture->height() == size.y)
    )
    {
        return;
    }

    m_viewport.width  = size.x;
    m_viewport.height = size.y;

    m_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::srgb8_alpha8,
            .sample_count    = 0,
            .width           = m_viewport.width,
            .height          = m_viewport.height
        }
    );
    m_texture->set_debug_label(m_debug_label);
    const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    gl::clear_tex_image(
        m_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label(m_debug_label);

    update_framebuffer();
}

void Framebuffer_window::render(
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker
)
{
    if (
        (m_viewport.width < 1) ||
        (m_viewport.height < 0)
    )
    {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{m_debug_label};

    pipeline_state_tracker.execute(&m_pipeline);

    bind_resources();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);
    gl::draw_arrays     (m_pipeline.input_assembly->primitive_topology, 0, 4);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

void Framebuffer_window::imgui()
{
    if (
        m_texture &&
        (m_texture->width() > 0) &&
        (m_texture->height() > 0)
    )
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ImGui::Image(
            m_texture,
            ImVec2{
                static_cast<float>(m_viewport.width),
                static_cast<float>(m_viewport.height)
            },
            ImVec2{0, 1},
            ImVec2{1, 0}
        );
        ImGui::PopStyleVar();
    }
    update_framebuffer();
}

} // namespace editor
