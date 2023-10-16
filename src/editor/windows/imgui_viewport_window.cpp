#include "windows/imgui_viewport_window.hpp"

#include "editor_log.hpp"
#include "scene/viewport_window.hpp"

#include "erhe_imgui/imgui_viewport.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

[[nodiscard]] auto choose_depth_stencil_format() 
{
    gl::Internal_format formats[] = {
        gl::Internal_format::depth32f_stencil8,
        gl::Internal_format::depth24_stencil8,
        gl::Internal_format::depth_stencil,
        gl::Internal_format::stencil_index8,
        gl::Internal_format::depth_component32f,
        gl::Internal_format::depth_component,
        gl::Internal_format::depth_component16
    };

    for (const auto format : formats) {
        if (gl_helpers::has_depth(format)) {
            GLint depth_renderable{};
            gl::get_internalformat_iv(
                gl::Texture_target::texture_2d, format, gl::Internal_format_p_name::depth_renderable, 1,
                &depth_renderable
            );
            if (depth_renderable == GL_FALSE) {
                continue;
            }
        }
        if (gl_helpers::has_stencil(format)) {
            GLint stencil_renderable{};
            gl::get_internalformat_iv(
                gl::Texture_target::texture_2d, format, gl::Internal_format_p_name::stencil_renderable, 1,
                &stencil_renderable
            );
            if (stencil_renderable == GL_FALSE) {
                continue;
            }
        }
        return format;
    }
    return gl::Internal_format::depth_component; // fallback
}

Imgui_viewport_window::Imgui_viewport_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    erhe::rendergraph::Rendergraph&         rendergraph,
    const std::string_view                  name,
    const std::string_view                  ini_label,
    const std::shared_ptr<Viewport_window>& viewport_window
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, name, ini_label}
    , erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .name                 = std::string{name},
            .input_key            = erhe::rendergraph::Rendergraph_node_key::viewport,
            .output_key           = erhe::rendergraph::Rendergraph_node_key::window,
            .color_format         = gl::Internal_format::rgba16f,
            .depth_stencil_format = choose_depth_stencil_format()
        }
    }
    , m_viewport_window{viewport_window}
{
    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = 0;
    m_viewport.height = 0;

    register_input(
        erhe::rendergraph::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    // "rendertarget texture" is slot / pseudo-resource which allows use rendergraph
    // connection to make Rendertarget_imgui_viewport a dependency for Imgui_viewport,
    // forcing correct rendering order; Rendertarget_imgui_viewport must be rendered
    // before Imgui_viewport.
    //
    // TODO Texture dependencies should be handled in a generic way.
    register_input(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        "rendertarget texture",
        erhe::rendergraph::Rendergraph_node_key::rendertarget_texture
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_viewport_window a dependency for (Window) Imgui_viewport,
    // forcing correct rendering order; Imgui_viewport_window must be rendered before
    // (Window) Imgui_viewport.
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(
        erhe::rendergraph::Resource_routing::None,
        "window",
        erhe::rendergraph::Rendergraph_node_key::window
    );

    show();
}

[[nodiscard]] auto Imgui_viewport_window::viewport_window() const -> std::shared_ptr<Viewport_window>
{
    return m_viewport_window.lock();
}

[[nodiscard]] auto Imgui_viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

void Imgui_viewport_window::on_mouse_move(glm::vec2 mouse_position_in_window)
{
    auto viewport_window = m_viewport_window.lock();
    if (!viewport_window) {
        return;
    }
    const auto mouse_position_in_viewport = viewport_window->viewport_from_window(mouse_position_in_window);
    viewport_window->update_pointer_2d_position(mouse_position_in_viewport);
    viewport_window->update_hover();
}

void Imgui_viewport_window::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.0, 0.0, 0.0, 0.0});
#endif
}

void Imgui_viewport_window::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
#endif
}

void Imgui_viewport_window::set_viewport(
    erhe::imgui::Imgui_viewport* imgui_viewport
)
{
    Imgui_window::set_viewport(imgui_viewport);
}

[[nodiscard]] auto Imgui_viewport_window::get_consumer_input_viewport(
    const erhe::rendergraph::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::math::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::rendergraph::Rendergraph_node_key::window); TODO
    return m_viewport;
}

[[nodiscard]] auto Imgui_viewport_window::get_producer_output_viewport(
    const erhe::rendergraph::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::math::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::rendergraph::Rendergraph_node_key::window); TODO
    return m_viewport;
}

void Imgui_viewport_window::toolbar(bool& hovered)
{
    const auto viewport_window = m_viewport_window.lock();
    if (!viewport_window) {
        return;
    }

    if (viewport_window->viewport_toolbar()) {
        hovered = true;
    }
}

void Imgui_viewport_window::hidden()
{
    Rendergraph_node::set_enabled(false);
}

void Imgui_viewport_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    Rendergraph_node::set_enabled(true);

    const auto viewport_window = m_viewport_window.lock();
    if (!viewport_window) {
        return;
    }

    const auto size = ImGui::GetContentRegionAvail();

    m_viewport.width  = std::max(1, static_cast<int>(size.x));
    m_viewport.height = std::max(1, static_cast<int>(size.y));

    const auto& color_texture = get_consumer_input_texture(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    if (color_texture) {
        const int texture_width  = color_texture->width();
        const int texture_height = color_texture->height();

        if ((texture_width >= 1) && (texture_height >= 1)) {
            SPDLOG_LOGGER_TRACE(
                log_render,
                "Imgui_viewport_window::imgui() rendering texture {} {}",
                texture->gl_name(),
                texture->debug_label()
            );
            image(
                color_texture,
                static_cast<int>(size.x),
                static_cast<int>(size.y)
            );
            m_is_hovered = ImGui::IsItemHovered();
            const auto rect_min = ImGui::GetItemRectMin();
            const auto rect_max = ImGui::GetItemRectMax();
            //ERHE_VERIFY(m_viewport.width  == static_cast<int>(rect_max.x - rect_min.x));
            //ERHE_VERIFY(m_viewport.height == static_cast<int>(rect_max.y - rect_min.y));

            viewport_window->set_window_viewport(
                erhe::math::Viewport{
                    static_cast<int>(rect_min.x),
                    static_cast<int>(rect_min.y),
                    m_viewport.width,
                    m_viewport.height
                }
            );
        }
    } else {
        m_is_hovered = false;
        viewport_window->set_window_viewport(erhe::math::Viewport{});
    }
    viewport_window->set_is_hovered(m_is_hovered);

    //m_viewport_config.imgui();
#endif
}

auto Imgui_viewport_window::want_mouse_events() const -> bool
{
    return true;
}

auto Imgui_viewport_window::want_keyboard_events() const -> bool
{
    return true;
}

} // namespace editor
