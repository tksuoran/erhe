// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/imgui_window_scene_view_node.hpp"
#include "scene/content_library.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/brushes/brush_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_primitive/material.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

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

Imgui_window_scene_view_node::Imgui_window_scene_view_node(
    erhe::imgui::Imgui_renderer&                imgui_renderer,
    erhe::imgui::Imgui_windows&                 imgui_windows,
    erhe::rendergraph::Rendergraph&             rendergraph,
    Editor_context&                             editor_context,
    const std::string_view                      name,
    const std::string_view                      ini_label,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view
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
    , m_editor_context{editor_context}
    , m_viewport_scene_view{viewport_scene_view}
{
    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = 0;
    m_viewport.height = 0;

    register_input(erhe::rendergraph::Routing::Resource_provided_by_consumer, "imgui_window_scene_view_node", erhe::rendergraph::Rendergraph_node_key::viewport);

    // "rendertarget texture" is slot / pseudo-resource which allows use rendergraph
    // connection to make Rendertarget_imgui_viewport a dependency for Imgui_host,
    // forcing correct rendering order; Rendertarget_imgui_viewport must be rendered
    // before Imgui_host.
    //
    // TODO Texture dependencies should be handled in a generic way.
    register_input(erhe::rendergraph::Routing::Resource_provided_by_producer, "rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_window_scene_view_node a dependency for (Window) Imgui_host,
    // forcing correct rendering order; Imgui_window_scene_view_node must be rendered before
    // (Window) Imgui_host.
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(erhe::rendergraph::Routing::None, "window", erhe::rendergraph::Rendergraph_node_key::window);

    show_window();
}

auto Imgui_window_scene_view_node::viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>
{
    return m_viewport_scene_view.lock();
}

void Imgui_window_scene_view_node::on_mouse_move(glm::vec2 mouse_position_in_window)
{
    SPDLOG_LOGGER_TRACE(log_scene_view, "{} on_mouse_move({}, {})", get_name(), mouse_position_in_window.x, mouse_position_in_window.y);
    auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    const auto mouse_position_in_viewport = viewport_scene_view->viewport_from_window(mouse_position_in_window);
    viewport_scene_view->update_pointer_2d_position(mouse_position_in_viewport);
}

void Imgui_window_scene_view_node::update_hover_info()
{
    auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    viewport_scene_view->update_hover();
}

void Imgui_window_scene_view_node::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.0, 0.0, 0.0, 0.0});
#endif
}

void Imgui_window_scene_view_node::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
#endif
}

void Imgui_window_scene_view_node::set_imgui_host(erhe::imgui::Imgui_host* imgui_host)
{
    Imgui_window::set_imgui_host(imgui_host);
}

auto Imgui_window_scene_view_node::get_consumer_input_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return m_viewport;
}

auto Imgui_window_scene_view_node::get_producer_output_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return m_viewport;
}

void Imgui_window_scene_view_node::toolbar(bool& hovered)
{
    const auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }

    if (viewport_scene_view->viewport_toolbar()) {
        hovered = true;
    }
}

void Imgui_window_scene_view_node::hidden()
{
    Rendergraph_node::set_enabled(false);
}

void Imgui_window_scene_view_node::cancel_brush_drag_and_drop()
{
    m_brush_drag_and_drop_active = false;
    m_editor_context.brush_tool->preview_drag_and_drop({});
}

void Imgui_window_scene_view_node::drag_and_drop_target(float min_x, float min_y, float max_x, float max_y)
{
    const ImGuiID drag_target_id = ImGui::GetID(static_cast<const void*>(this));
    ImRect rect{min_x, min_y, max_x, max_y};
    if (ImGui::BeginDragDropTargetCustom(rect, drag_target_id)) {
        ERHE_DEFER( ImGui::EndDragDropTarget(); );

        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Content_library_node", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload == nullptr) {
            cancel_brush_drag_and_drop();
            return;
        }

        const erhe::Item_base* item_base = *(static_cast<erhe::Item_base**>(payload->Data));
        const Content_library_node* node = dynamic_cast<const Content_library_node*>(item_base);
        if (node == nullptr) {
            cancel_brush_drag_and_drop();
            return;
        }

        std::shared_ptr<erhe::Item_base> item = node->item;
        if (!item) {
            cancel_brush_drag_and_drop();
            return;
        }

        std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(item);
        if (brush) {
            if (payload->Preview) {
                // TODO preview
                m_editor_context.brush_tool->preview_drag_and_drop(brush);
            }
            if (payload->Delivery) {
                m_editor_context.brush_tool->try_insert(brush.get());
            }
        } else {
            cancel_brush_drag_and_drop();
        }
        //// const erhe::primitive::Material* material = dynamic_cast<const erhe::primitive::Material*>(item);
    } else {
        cancel_brush_drag_and_drop();
    }
}

void Imgui_window_scene_view_node::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    Rendergraph_node::set_enabled(true);

    const auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }

    const auto size = ImGui::GetContentRegionAvail();

    m_viewport.width  = std::max(1, static_cast<int>(size.x));
    m_viewport.height = std::max(1, static_cast<int>(size.y));

    const auto& color_texture = get_consumer_input_texture(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    if (color_texture) {
        const int texture_width  = color_texture->width();
        const int texture_height = color_texture->height();

        if ((texture_width >= 1) && (texture_height >= 1)) {
            SPDLOG_LOGGER_TRACE(
                log_render,
                "Imgui_window_scene_view_node::imgui() rendering texture {} {}",
                color_texture->gl_name(),
                color_texture->debug_label()
            );
            draw_image(color_texture, static_cast<int>(size.x), static_cast<int>(size.y));
            bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            SPDLOG_LOGGER_TRACE(log_scene_view, "{} ImGui::IsItemHovered() = {}", get_name(), is_hovered);
            set_is_window_hovered(is_hovered);
            viewport_scene_view->set_is_scene_view_hovered(is_hovered);
            const auto rect_min = ImGui::GetItemRectMin();
            const auto rect_max = ImGui::GetItemRectMax();
            drag_and_drop_target(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
            //ERHE_VERIFY(m_viewport.width  == static_cast<int>(rect_max.x - rect_min.x));
            //ERHE_VERIFY(m_viewport.height == static_cast<int>(rect_max.y - rect_min.y));

            viewport_scene_view->set_window_viewport(
                erhe::math::Viewport{
                    static_cast<int>(rect_min.x),
                    static_cast<int>(rect_min.y),
                    m_viewport.width,
                    m_viewport.height
                }
            );
        }
    } else {
        SPDLOG_LOGGER_TRACE(log_scene_view, "{} no color texture", get_name());
        set_is_window_hovered(false);
        viewport_scene_view->set_is_scene_view_hovered(false);
        viewport_scene_view->set_window_viewport(erhe::math::Viewport{});
    }

    //m_viewport_config.imgui();
#endif
}

auto Imgui_window_scene_view_node::want_mouse_events() const -> bool
{
    return true;
}

auto Imgui_window_scene_view_node::want_keyboard_events() const -> bool
{
    return true;
}

} // namespace editor
