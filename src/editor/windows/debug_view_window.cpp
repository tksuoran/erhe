// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/debug_view_window.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui_helpers.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp" // TODO move light layer to separate header
#include "erhe/toolkit/profile.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace editor
{

Debug_view_render_graph_node::Debug_view_render_graph_node(
    const std::shared_ptr<Debug_view_window>& debug_view_window
)
    : erhe::application::Render_graph_node{"Debug View"     }
    , m_debug_view_window                 {debug_view_window}
{
}

void Debug_view_render_graph_node::execute_render_graph_node()
{
    m_debug_view_window->render();
}

Debug_view_window::Debug_view_window()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
{
}

Debug_view_window::~Debug_view_window() noexcept
{
}

void Debug_view_window::declare_required_components()
{
    m_programs = require<Programs>();

    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
}

void Debug_view_window::initialize_component()
{
    const erhe::application::Scoped_gl_context gl_context{
        get<erhe::application::Gl_context_provider>()
    };

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    m_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        erhe::graphics::Vertex_input_state_data{}
    );

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
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_forward_renderer       = get<Forward_renderer>();
    m_shadow_renderer        = get<Shadow_renderer >();
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

void Debug_view_window::render()
{
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render()");

    ERHE_PROFILE_FUNCTION

    if (
        !m_shadow_renderer ||
        (m_viewport.width < 1) ||
        (m_viewport.height < 1)
    )
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - skipped: no shadow renderer or empty viewport");
        return;
    }

    const auto& scene_root = m_shadow_renderer->scene_root();
    if (!scene_root)
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - skipped: no shadow scene root");
        return;
    }

    if (m_shadow_renderer->light_projections().light_projection_transforms.empty())
    {
        return;
    }

    if (m_light_index >= m_shadow_renderer->light_projections().light_projection_transforms.size())
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - skipped: invalid selected light index");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Debug View"};

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());

    const erhe::scene::Light_projection_transforms& light_projection_transforms = m_shadow_renderer->light_projections().light_projection_transforms.at(m_light_index);

    const auto& layers     = scene_root->layers();

    m_forward_renderer->render_fullscreen(
        Forward_renderer::Render_parameters{
            .viewport          = m_viewport,
            .mesh_spans        = {},
            .lights            = layers.light()->lights,
            .light_projections = m_shadow_renderer->light_projections(),
            .materials         = {},
            .passes            = { &m_renderpass }
        },
        light_projection_transforms.light
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - done");
}

// Update texture to match ImGui window/ viewport size
void Debug_view_window::update_framebuffer(
    const glm::vec2 available_size
)
{
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::update_framebuffer(available_size = {})", available_size);

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    using Texture     = erhe::graphics::Texture;
    using Framebuffer = erhe::graphics::Framebuffer;

    //const ImVec2 content_region_available = ImGui::GetContentRegionAvail();
    //const ImVec2 content_region_max_size  = ImGui::GetContentRegionMax();

    if (
        (available_size.x <     1.0f) ||
        (available_size.y <     1.0f) ||
        (available_size.x > 65536.0f) || // TODO Check against implementation max texture 2d size
        (available_size.y > 65536.0f)
    )
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::update_framebuffer(): invalid size");
        return;
    }
    const float min_size = std::min(available_size.x, available_size.y);
    const int   int_size = static_cast<int>(min_size);

    if (
        m_texture &&
        (m_texture->width()  == int_size) &&
        (m_texture->height() == int_size)
    )
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::update_framebuffer(): early out - old texture size matches");
        return;
    }

    m_viewport.width  = int_size;
    m_viewport.height = int_size;

    auto new_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::srgb8_alpha8,
            .sample_count    = 0,
            .width           = int_size,
            .height          = int_size
        }
    );
    new_texture->set_debug_label("Debug View");
    const float clear_value[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    gl::clear_tex_image(
        new_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, new_texture.get());
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("Debug View");
    SPDLOG_LOGGER_TRACE(
        log_render,
        "Debug_view_window::update_framebuffer(): done - new texture {}, size = {}",
        new_texture->gl_name(),
        int_size
    );

    m_texture = new_texture;
#endif
}

void Debug_view_window::imgui()
{
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui()");

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (!m_shadow_renderer)
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - skipped - no shadow renderer");
        return;
    }

    const auto& light_projections           = m_shadow_renderer->light_projections();
    const auto& light_projection_transforms = light_projections.light_projection_transforms;
    const int count = static_cast<int>(light_projection_transforms.size());
    for (int i = 0; i < count; ++i)
    {
        const auto& light_projection_transform = light_projection_transforms.at(i);
        ERHE_VERIFY(light_projection_transform.light != nullptr);

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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", light_projection_transform.light->name().c_str());
        }
    }

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    const int   int_image_size = static_cast<int>(image_size);

    if (
        m_texture &&
        (m_texture->width () > 0) &&
        (m_texture->height() > 0) &&
        (int_image_size > 0)
    )
    {
        auto cursor_position = ImGui::GetCursorPos();
        cursor_position.x += (available_size.x - image_size) / 2.0f;
        cursor_position.y += (available_size.y - image_size) / 2.0f;
        ImGui::SetCursorPos(cursor_position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        image(m_texture, int_image_size, int_image_size);
        // bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    }
    else
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - skipped - no texture or empty size");
    }
    update_framebuffer(glm::vec2{available_size.x, available_size.y});
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - done");
#endif
}

} // namespace editor
