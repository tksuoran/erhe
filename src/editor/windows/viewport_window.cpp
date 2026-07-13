#include "windows/viewport_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "asset_browser/asset_browser.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_root.hpp"
#include "time.hpp"
#include "tools/fly_camera_tool.hpp"

#include "erhe_math/math_util.hpp"

#define IMVIEWGUIZMO_IMPLEMENTATION
#include "ImViewGuizmo.h"

#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/mesh_component_selection_tool.hpp"
#include "tools/selection_tool.hpp"
#include "windows/active_scene_highlight.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"

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
    const int                                                   window_slot,
    const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, name, ini_label}
    , m_app_context            {app_context}
    , m_viewport_scene_view    {viewport_scene_view}
    , m_rendergraph_output_node{rendergraph_output_node}
    , m_window_slot            {window_slot}
{
    show_window();
}

Viewport_window::~Viewport_window() noexcept = default;

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
    update_title_from_scene();
    {
        const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
        const std::shared_ptr<Scene_root> scene_root = viewport_scene_view ? viewport_scene_view->get_scene_root() : std::shared_ptr<Scene_root>{};
        m_active_scene_tint_count = push_active_scene_window_tint(m_app_context, scene_root.get());
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.0, 0.0, 0.0, 0.0});
}

void Viewport_window::update_title_from_scene()
{
    // Show the name of the Scene item this viewport is looking at. The
    // "###Viewport_window N" suffix (composed by Scene_views::create_viewport_window)
    // keeps the ImGui window identity stable while the visible part changes;
    // a title without that suffix is left alone, since retitling it would
    // change the window identity. Reformats only when the name changes, so
    // steady-state frames do not allocate.
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = viewport_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }
    const std::string& scene_name = scene_root->get_name();
    if (scene_name.empty() || (scene_name == m_shown_scene_name)) {
        return;
    }
    const std::size_t id_part = m_title.find("###");
    if (id_part == std::string::npos) {
        return;
    }
    m_shown_scene_name = scene_name;
    m_title = scene_name + m_title.substr(id_part);
}

void Viewport_window::on_end()
{
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (m_active_scene_tint_count > 0) {
        ImGui::PopStyleColor(m_active_scene_tint_count);
        m_active_scene_tint_count = 0;
    }
}

void Viewport_window::set_imgui_host(erhe::imgui::Imgui_host* imgui_host)
{
    Imgui_window::set_imgui_host(imgui_host);
}

void Viewport_window::draw_toolbar()
{
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }

    if (ImGui::BeginMenuBar()) {
        viewport_scene_view->viewport_toolbar();
        ImGui::EndMenuBar();
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

auto Viewport_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_MenuBar;
}

void Viewport_window::cancel_brush_drag_and_drop()
{
    m_brush_drag_and_drop_active = false;
    m_app_context.brush_tool->preview_drag_and_drop({});
}

namespace {

// Draw a world-space AABB as a wireframe box into the current ImGui window
// draw list, projecting through the viewport camera. Edges with an endpoint
// outside the depth range (behind the camera) are skipped.
void draw_world_aabb_overlay(const Viewport_scene_view& viewport_scene_view, const erhe::math::Aabb& aabb, const bool bottom_left_origin)
{
    const erhe::math::Viewport& window_viewport     = viewport_scene_view.get_window_viewport();
    const erhe::math::Viewport& projection_viewport = viewport_scene_view.get_projection_viewport();
    if ((projection_viewport.width < 1) || (projection_viewport.height < 1)) {
        return;
    }
    const float scale_x = static_cast<float>(window_viewport.width)  / static_cast<float>(projection_viewport.width);
    const float scale_y = static_cast<float>(window_viewport.height) / static_cast<float>(projection_viewport.height);

    const glm::vec3 corners[8] = {
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.max.y, aabb.max.z },
        { aabb.min.x, aabb.max.y, aabb.max.z }
    };
    ImVec2 screen_points[8];
    bool   point_ok[8];
    for (int i = 0; i < 8; ++i) {
        const std::optional<glm::vec3> projected = viewport_scene_view.project_to_viewport(corners[i]);
        point_ok[i] = projected.has_value() && (projected->z >= 0.0f) && (projected->z <= 1.0f);
        if (!point_ok[i]) {
            continue;
        }
        // Inverse of Viewport_scene_view::get_viewport_from_window: viewport
        // coordinates back to ImGui screen coordinates, flipping Y for
        // bottom-left-origin (OpenGL) framebuffers.
        const float viewport_x = (projected->x - static_cast<float>(projection_viewport.x)) * scale_x;
        const float viewport_y = (projected->y - static_cast<float>(projection_viewport.y)) * scale_y;
        const float content_y  = bottom_left_origin ? (static_cast<float>(window_viewport.height) - viewport_y) : viewport_y;
        screen_points[i] = ImVec2{
            static_cast<float>(window_viewport.x) + viewport_x,
            static_cast<float>(window_viewport.y) + content_y
        };
    }

    constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // near face
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // far face
        {0, 4}, {1, 5}, {2, 6}, {3, 7}  // connecting edges
    };
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    constexpr ImU32 edge_color = IM_COL32(255, 210, 80, 255);
    for (const auto& edge : edges) {
        if (point_ok[edge[0]] && point_ok[edge[1]]) {
            draw_list->AddLine(screen_points[edge[0]], screen_points[edge[1]], edge_color, 2.0f);
        }
    }
}

} // namespace

void Viewport_window::gltf_drag_preview_and_drop(Asset_file_gltf& gltf, const bool preview, const bool delivery)
{
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if ((source_path == nullptr) || (m_app_context.prefab_library == nullptr)) {
        return;
    }

    // JSON-level scan (cheap, cached on the asset node) providing the AABB
    // for the preview box and bottom-snap placement.
    ensure_scanned(gltf);

    // Anchor at the hovered surface point (scene content or grid); when the
    // cursor misses both, fall back to a point in front of the camera.
    glm::vec3 anchor{0.0f};
    const Hover_entry* hover = viewport_scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if ((hover != nullptr) && hover->valid && hover->position.has_value()) {
        anchor = hover->position.value();
    } else {
        const std::optional<glm::vec3> in_front = viewport_scene_view->get_control_position_in_world_at_distance(5.0f);
        if (!in_front.has_value()) {
            return;
        }
        anchor = in_front.value();
    }

    // Snap the asset so its AABB rests bottom-centered on the anchor.
    glm::vec3 translation = anchor;
    const bool has_bounds = gltf.bounding_box.has_value() && gltf.bounding_box->is_valid();
    if (has_bounds) {
        const erhe::math::Aabb& aabb  = gltf.bounding_box.value();
        const glm::vec3         center = aabb.center();
        translation = anchor - glm::vec3{center.x, aabb.min.y, center.z};
    }

    if (preview && has_bounds) {
        const erhe::math::Aabb world_aabb{
            .min = gltf.bounding_box->min + translation,
            .max = gltf.bounding_box->max + translation
        };
        const bool bottom_left_origin =
            m_app_context.graphics_device->get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left;
        draw_world_aabb_overlay(*viewport_scene_view, world_aabb, bottom_left_origin);
    }

    if (delivery) {
        const std::shared_ptr<Prefab> prefab = m_app_context.prefab_library->get_or_load(*source_path);
        if (!prefab) {
            log_scene->warn("Dropped glTF could not be loaded as a prefab: {}", source_path->string());
            return;
        }
        const std::shared_ptr<Scene_root> scene_root = viewport_scene_view->get_scene_root();
        if (!scene_root) {
            return;
        }
        instantiate_prefab(m_app_context, prefab, *scene_root, erhe::math::create_translation<float>(translation));
    }
}

void Viewport_window::drag_and_drop_target(float min_x, float min_y, float max_x, float max_y)
{
    const ImGuiID drag_target_id = ImGui::GetID(static_cast<const void*>(this));
    ImRect rect{min_x, min_y, max_x, max_y};
    if (ImGui::BeginDragDropTargetCustom(rect, drag_target_id)) {
        ERHE_DEFER( ImGui::EndDragDropTarget(); );

        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Content_library_node", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload == nullptr) {
            // glTF assets dragged from the Asset browser: AABB wireframe
            // preview while hovering, instantiate as a prefab on drop.
            const ImGuiPayload* gltf_payload = ImGui::AcceptDragDropPayload(
                Asset_file_gltf::static_type_name.data(),
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
            );
            if (gltf_payload != nullptr) {
                erhe::Item_base* item_base = *(static_cast<erhe::Item_base**>(gltf_payload->Data));
                Asset_file_gltf* gltf = dynamic_cast<Asset_file_gltf*>(item_base);
                if (gltf != nullptr) {
                    gltf_drag_preview_and_drop(*gltf, gltf_payload->Preview, gltf_payload->Delivery);
                }
            }
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

        auto brush    = std::dynamic_pointer_cast<Brush>(item);
        auto material = std::dynamic_pointer_cast<erhe::primitive::Material>(item);
        if (brush) {
            if (payload->Preview) {
                m_app_context.brush_tool->preview_drag_and_drop(brush);
            }
            if (payload->Delivery) {
                m_app_context.brush_tool->try_insert(brush.get());
            }
        } else if (material) {
            // TODO payload->Preview
            if (payload->Delivery) {
                m_app_context.material_paint_tool->from_drag_and_drop(material);
            }
            cancel_brush_drag_and_drop();
        }
        //// const erhe::primitive::Material* material = dynamic_cast<const erhe::primitive::Material*>(item);
    } else {
        cancel_brush_drag_and_drop();
    }
}

void Viewport_window::imgui()
{
    // Giving this viewport focus makes its scene the active scene
    // (edge-triggered: selection changes elsewhere may activate another
    // scene while this window stays focused, and must not be fought).
    {
        const bool focused        = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool focus_gained   = focused && !m_was_focused;
        // Update m_was_focused before set_active_scene_root: the activation
        // message is handled synchronously (Scene_views focuses a viewport
        // window of the newly active scene unless one is already focused),
        // so this window must already report itself focused or the handler
        // would redundantly focus another window showing the same scene.
        m_was_focused = focused;
        if (focus_gained) {
            const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
            const std::shared_ptr<Scene_root> scene_root = viewport_scene_view ? viewport_scene_view->get_scene_root() : std::shared_ptr<Scene_root>{};
            if (scene_root && (m_app_context.selection != nullptr)) {
                m_app_context.selection->set_active_scene_root(scene_root);
            }
        }
    }

    draw_toolbar();
    ImGui::BeginChildEx(
        "Viewport_window::imgui()", // name
        ImGuiID{1},                 // id
        ImVec2{0.0f, 0.0f},         // size_arg
        ImGuiChildFlags_None,       // child_flags

        // window_flags:
        ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize          | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings
    );
    ImVec2 viewport_start = ImGui::GetCursorPos();
    imgui_viewport();
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view ? viewport_scene_view->get_camera() : nullptr;
    erhe::scene::Node* node = camera ? camera->get_node() : nullptr;
    if (viewport_scene_view && viewport_scene_view->get_show_navigation_gizmo() && (node != nullptr)) {
        ImGui::SetCursorPos(viewport_start);
        const ImVec2 after_toolbar_cursor_pos = ImGui::GetCursorPos();

        erhe::scene::Trs_transform transform = node->world_from_node_transform();
        glm::quat camera_rotation = transform.get_rotation();
        glm::vec3 camera_position = transform.get_translation();

        ImVec2 window_position = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImViewGuizmo::Style& style = ImViewGuizmo::GetStyle();
        style.snapAnimationDurationNs = 200'000'000;
        const float rotate_radius = style.bigCircleRadius * style.scale;
        const float button_radius = style.toolButtonRadius * style.scale;
        ImVec2 position = window_position + ImVec2{window_size.x - rotate_radius, after_toolbar_cursor_pos.y + rotate_radius};
        bool modified = false;
        const int64_t time_ns = m_app_context.time->get_host_system_time_ns();

        // Draw + hover only. Input (orbit/zoom/pan/snap) is driven through erhe::commands
        // by Navigation_gizmo_tool, which writes the camera directly. The only camera
        // change applied here is the snap animation advanced inside draw_rotate.
        ImViewGuizmo::Context& gizmo = viewport_scene_view->get_navigation_gizmo();
        gizmo.BeginFrame();
        if (gizmo.draw_rotate(time_ns, camera_position, camera_rotation, position)) {
            modified = true;
        }
        position.y += rotate_radius;
        position.x = window_position.x + window_size.x - 2.0f * button_radius;
        gizmo.draw_zoom(position);
        position.y += 2.0f * button_radius;
        gizmo.draw_pan(position);

        if (modified) {
            transform.set_translation(camera_position);
            transform.set_rotation(camera_rotation);
            node->set_world_from_node(transform);
            m_app_context.app_message_bus->node_touched.send_message(
                Node_touched_message{
                    .source = Node_touch_source::navigation_gizmo,
                    .node   = node
                }
            );
        }

        m_request_cursor_relative_hold = gizmo.IsUsing();
    }

    // Selection gesture overlay (box rubber-band / brush circle), drawn on top
    // of the viewport image in this child window's draw list.
    if ((m_app_context.mesh_component_selection_tool != nullptr) && viewport_scene_view) {
        m_app_context.mesh_component_selection_tool->draw_gesture_overlay(viewport_scene_view.get());
    }

    ImGui::EndChild();
}

void Viewport_window::imgui_viewport()
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("Viewport_window::imgui()");

    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        log_frame->warn(" - no viewport_scene_view");
        return;
    }

    viewport_scene_view->set_enabled(true);

    // Single source of truth for the viewport size: the same width/height
    // ints are used for the ImGui image quad and for the scene view's window
    // viewport (which later drives the render target texture allocation).
    // Deriving the size a second time from the item rect would invite drift
    // between the texture size and the quad size, which shows up as a
    // stretched / never texel-aligned viewport image.
    const ImVec2 size   = ImGui::GetContentRegionAvail();
    const int    width  = static_cast<int>(size.x);
    const int    height = static_cast<int>(size.y);
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node = m_rendergraph_output_node.lock();
    if (rendergraph_output_node) {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Viewport_window::imgui() rendering texture {} {}",
            color_texture->gl_name(),
            color_texture->debug_label()
        );
        m_app_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = rendergraph_output_node,
                .width             = width,
                .height            = height,
                .uv0               = m_app_context.imgui_renderer->get_rtt_uv0(),
                .uv1               = m_app_context.imgui_renderer->get_rtt_uv1(),
                .debug_label       = "Viewport_window::imgui()"
            }
        );

        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 rect_max = ImGui::GetItemRectMax();
        viewport_scene_view->set_window_viewport(
            erhe::math::Viewport{
                static_cast<int>(rect_min.x),
                static_cast<int>(rect_min.y),
                width,
                height
            }
        );

#if !defined(NDEBUG)
        // Regression guard: the output texture is reallocated later this
        // frame (during rendergraph execution) from the viewport set above,
        // so on a stable frame (size unchanged since last frame) it must
        // already match width/height. A mismatch here means the texture size
        // and the quad size have drifted apart again.
        const erhe::graphics::Texture* output_texture = rendergraph_output_node->get_referenced_texture();
        if (
            (output_texture != nullptr) &&
            (width == m_last_viewport_width) && (height == m_last_viewport_height) &&
            ((output_texture->get_width() != width) || (output_texture->get_height() != height))
        ) {
            // Rate-limit: log only when the mismatching texture size changes
            if ((output_texture->get_width() != m_reported_mismatch_width) || (output_texture->get_height() != m_reported_mismatch_height)) {
                log_frame->warn(
                    "Viewport_window '{}': output texture {}x{} != quad {}x{}",
                    get_title(),
                    output_texture->get_width(),
                    output_texture->get_height(),
                    width,
                    height
                );
                m_reported_mismatch_width  = output_texture->get_width();
                m_reported_mismatch_height = output_texture->get_height();
            }
        }
        m_last_viewport_width  = width;
        m_last_viewport_height = height;
#endif

        const bool imgui_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenBlockedByPopup
        );
        // Keep reporting as hovered while this viewport holds the mouse-drag pointer capture,
        // so a drag that started here keeps tracking the cursor after it leaves the rect.
        const bool owns_pointer_capture =
            (m_app_context.scene_views != nullptr) &&
            m_app_context.scene_views->owns_pointer_capture(viewport_scene_view.get());
        m_viewport_child_window_hovered = imgui_hovered || owns_pointer_capture;
        m_viewport_child_window_focused = ImGui::IsWindowFocused();

        viewport_scene_view->set_is_scene_view_hovered(m_viewport_child_window_hovered);
        drag_and_drop_target(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
    } else {
        ImGui::Dummy(ImVec2{static_cast<float>(width), static_cast<float>(height)});
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        viewport_scene_view->set_window_viewport(
            erhe::math::Viewport{
                static_cast<int>(rect_min.x),
                static_cast<int>(rect_min.y),
                width,
                height
            }
        );
        m_viewport_child_window_focused = ImGui::IsWindowFocused();

        // log_frame->warn("{} no rendergraph output node", get_title());
        viewport_scene_view->set_is_scene_view_hovered(false);
    }
}

auto Viewport_window::want_mouse_events() const -> bool
{
    return m_viewport_child_window_hovered;
}

auto Viewport_window::want_keyboard_events() const -> bool
{
    return m_viewport_child_window_hovered;
}

auto Viewport_window::want_cursor_relative_hold() const -> bool
{
    // TODO Consider m_viewport_child_window_focused ?
    if (m_request_cursor_relative_hold) {
        return true;
    }
    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return false;
    }
    return viewport_scene_view->get_cursor_relative_hold();
}

}
