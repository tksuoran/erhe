// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/viewport_window.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Viewport_window::Viewport_window(
    erhe::imgui::Imgui_renderer&                                imgui_renderer,
    erhe::imgui::Imgui_windows&                                 imgui_windows,
    const std::shared_ptr<erhe::rendergraph::Rendergraph_node>& rendergraph_output_node,
    App_context&                                                app_context,
    const std::string_view                                      name,
    const std::string_view                                      ini_label,
    const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, name, ini_label}
    , m_app_context            {app_context}
    , m_viewport_scene_view    {viewport_scene_view}
    , m_rendergraph_output_node{rendergraph_output_node}
{
    show_window();
}

auto Viewport_window::viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>
{
    return m_viewport_scene_view.lock();
}

void Viewport_window::on_mouse_move(glm::vec2 mouse_position_in_window)
{
    SPDLOG_LOGGER_TRACE(log_scene_view, "{} on_mouse_move({}, {})", get_name(), mouse_position_in_window.x, mouse_position_in_window.y);
    auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    const auto mouse_position_in_viewport = viewport_scene_view->get_viewport_from_window(mouse_position_in_window);
    viewport_scene_view->update_pointer_2d_position(mouse_position_in_viewport);
}

void Viewport_window::update_hover_info()
{
    auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    viewport_scene_view->update_hover();
}

void Viewport_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.0, 0.0, 0.0, 0.0});
}

void Viewport_window::on_end()
{
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Viewport_window::set_imgui_host(erhe::imgui::Imgui_host* imgui_host)
{
    Imgui_window::set_imgui_host(imgui_host);
}

void Viewport_window::toolbar(bool& hovered)
{
    const auto viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }

    if (viewport_scene_view->viewport_toolbar()) {
        hovered = true;
    }
}

void Viewport_window::hidden()
{
    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    viewport_scene_view->set_enabled(false);
}

void Viewport_window::cancel_brush_drag_and_drop()
{
    m_brush_drag_and_drop_active = false;
    m_app_context.brush_tool->preview_drag_and_drop({});
}

void Viewport_window::drag_and_drop_target(float min_x, float min_y, float max_x, float max_y)
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
                m_app_context.brush_tool->preview_drag_and_drop(brush);
            }
            if (payload->Delivery) {
                m_app_context.brush_tool->try_insert(brush.get());
            }
        } else {
            cancel_brush_drag_and_drop();
        }
        //// const erhe::primitive::Material* material = dynamic_cast<const erhe::primitive::Material*>(item);
    } else {
        cancel_brush_drag_and_drop();
    }
}

void Viewport_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }

    viewport_scene_view->set_enabled(true);

    const ImVec2 size = ImGui::GetContentRegionAvail();
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node = m_rendergraph_output_node.lock();
    const std::shared_ptr<erhe::graphics::Texture>& color_texture = 
        rendergraph_output_node 
            ? rendergraph_output_node->get_producer_output_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture)
            : std::shared_ptr<erhe::graphics::Texture>{};
    if (color_texture) {
        const int texture_width  = color_texture->get_width();
        const int texture_height = color_texture->get_height();

        if ((texture_width >= 1) && (texture_height >= 1)) {
            SPDLOG_LOGGER_TRACE(
                log_render,
                "Viewport_window::imgui() rendering texture {} {}",
                color_texture->gl_name(),
                color_texture->debug_label()
            );
            draw_image(color_texture, static_cast<int>(size.x), static_cast<int>(size.y));
            const ImVec2 rect_min = ImGui::GetItemRectMin();
            const ImVec2 rect_max = ImGui::GetItemRectMax();
            viewport_scene_view->set_window_viewport(
                erhe::math::Viewport{
                    static_cast<int>(rect_min.x),
                    static_cast<int>(rect_min.y),
                    static_cast<int>(rect_max.x - rect_min.x + 1.0f),
                    static_cast<int>(rect_max.y - rect_min.y + 1.0f)
                }
            );

            const bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            SPDLOG_LOGGER_TRACE(log_scene_view, "{} ImGui::IsItemHovered() = {}", get_name(), is_hovered);
            set_is_window_hovered(is_hovered);
            viewport_scene_view->set_is_scene_view_hovered(is_hovered);
            drag_and_drop_target(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
        }
    } else {
        ImGui::Dummy(size);
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 rect_max = ImGui::GetItemRectMax();
        viewport_scene_view->set_window_viewport(
            erhe::math::Viewport{
                static_cast<int>(rect_min.x),
                static_cast<int>(rect_min.y),
                static_cast<int>(rect_max.x - rect_min.x + 1.0f),
                static_cast<int>(rect_max.y - rect_min.y + 1.0f)
            }
        );

        SPDLOG_LOGGER_TRACE(log_scene_view, "{} no color texture", get_name());
        set_is_window_hovered(false);
        viewport_scene_view->set_is_scene_view_hovered(false);
    }
}

auto Viewport_window::want_mouse_events() const -> bool
{
    return true;
}

auto Viewport_window::want_keyboard_events() const -> bool
{
    return true;
}

}
