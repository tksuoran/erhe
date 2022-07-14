#include "windows/debug_view_window.hpp"

#include "scene/scene_root.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui_helpers.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp" // TODO move light layer to separate header
#include "erhe/toolkit/profile.hpp"

namespace editor
{

Debug_view_window::Debug_view_window()
    : erhe::components::Component          {c_label}
    , erhe::application::Framebuffer_window{c_title, c_label}
{
}

Debug_view_window::~Debug_view_window() noexcept
{
}

void Debug_view_window::declare_required_components()
{
    m_programs = require<Programs>();

    require<erhe::application::Imgui_windows>();
    require<erhe::application::Gl_context_provider>();
}

void Debug_view_window::initialize_component()
{
    Framebuffer_window::initialize(*m_components);

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    m_renderpass.pipeline.data = {
        .name           = "Debug_view",
        .shader_stages  = m_programs->debug_depth.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
    };
}

void Debug_view_window::post_initialize()
{
    m_forward_renderer       = get<Forward_renderer                    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_scene_root             = get<Scene_root                          >();
    m_shadow_renderer        = get<Shadow_renderer                     >();
}

auto Debug_view_window::get_size(
    glm::vec2 available_size
) const -> glm::vec2
{
    static_cast<void>(available_size);

    if (!m_shadow_renderer || !m_shadow_renderer->texture())
    {
        return {};
    }

    return glm::vec2{
        static_cast<float>(m_shadow_renderer->texture()->width()),
        static_cast<float>(m_shadow_renderer->texture()->height()),
    };
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

void Debug_view_window::render()
{
    ERHE_PROFILE_FUNCTION

    if (
        (m_viewport.width < 1) ||
        (m_viewport.height < 1)
    )
    {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{m_debug_label};

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());

    m_forward_renderer->render_fullscreen(
        Forward_renderer::Render_parameters{
            .viewport          = m_viewport,
            .mesh_spans        = {},
            .lights            = m_scene_root->light_layer()->lights,
            .light_projections = m_shadow_renderer->light_projections(),
            .materials         = m_scene_root->materials(),
            .passes            = { &m_renderpass }
        },
        m_light_index
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

void Debug_view_window::update_framebuffer()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (
        (
            m_texture &&
            (m_texture->width()  == m_viewport.width) &&
            (m_texture->height() == m_viewport.height)
        ) ||
        (m_viewport.width < 1) ||
        (m_viewport.height < 1)
    )
    {
        return;
    }

    using Texture = erhe::graphics::Texture;
    using Framebuffer = erhe::graphics::Framebuffer;
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
#endif
}

void Debug_view_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    const auto& light_projections = m_shadow_renderer->light_projections();
    const int count = static_cast<int>(light_projections.texture_from_world.size());
    for (int i = 0; i < count; ++i)
    {
        ImGui::SameLine();
        //ImGui::SetNextItemWidth(30.0f);
        std::string label = fmt::format("{}", i);
        //if (ImGui::Button(label.c_str()))
        if (
            erhe::application::make_button(
                label.c_str(),
                (m_light_index == i)
                    ? erhe::application::Item_mode::active
                    : erhe::application::Item_mode::normal
            )
        )
        {
            m_light_index = i;
        }
    }

    const auto size = ImGui::GetContentRegionAvail();

    if (
        m_texture &&
        (m_texture->width() > 0) &&
        (m_texture->height() > 0)
    )
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        image(
            m_texture,
            static_cast<int>(size.x),
            static_cast<int>(size.y)
        );
        m_is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    }

    m_viewport.width  = static_cast<int>(size.x);
    m_viewport.height = static_cast<int>(size.y);
    update_framebuffer();
#endif
}

} // namespace editor
