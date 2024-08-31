// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "rendertarget_imgui_host.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "rendertarget_mesh.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_action.hpp"
#endif

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include <GLFW/glfw3.h> // TODO Fix dependency ?

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace editor {

Rendertarget_imgui_host::Rendertarget_imgui_host(
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::rendergraph::Rendergraph& rendergraph,
    Editor_context&                 editor_context,
    Rendertarget_mesh*              rendertarget_mesh,
    const std::string_view          name,
    const bool                      imgui_ini
)
    : erhe::imgui::Imgui_host{
        rendergraph,
        imgui_renderer,
        name,
        imgui_ini,
        imgui_renderer.get_font_atlas()
    }
    , m_context          {editor_context}
    , m_rendertarget_mesh{rendertarget_mesh}
{
    register_output(erhe::rendergraph::Routing::Resource_provided_by_producer, "rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);

    imgui_renderer.use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io = m_imgui_context->IO;
    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");
    io.ConfigFlags             = io.ConfigFlags & ~ImGuiConfigFlags_DockingEnable;
    io.DisplaySize             = ImVec2{m_rendertarget_mesh->width(), m_rendertarget_mesh->height()};
    io.FontDefault             = imgui_renderer.vr_primary_font();
    io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};
    io.MouseDrawCursor         = editor_context.OpenXR;
    io.BackendPlatformUserData = this;
    io.BackendPlatformName     = "erhe rendertarget";
    io.MousePos                = ImVec2{-FLT_MAX, -FLT_MAX};
    io.MouseHoveredViewport    = 0;

    m_last_mouse_x = -FLT_MAX;
    m_last_mouse_y = -FLT_MAX;
}

Rendertarget_imgui_host::~Rendertarget_imgui_host() noexcept = default;

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> std::span<const T>
{
    return std::span<const T>(&value, 1);
}

auto Rendertarget_imgui_host::rendertarget_mesh() -> Rendertarget_mesh*
{
    return m_rendertarget_mesh;
}

auto Rendertarget_imgui_host::get_mutable_style() -> ImGuiStyle&
{
    return m_imgui_context->Style;
}

auto Rendertarget_imgui_host::get_style() const -> const ImGuiStyle&
{
    return m_imgui_context->Style;
}

auto Rendertarget_imgui_host::get_scale_value() const -> float
{
    return 2.0f;
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
auto Rendertarget_imgui_host::on_xr_boolean_event(const erhe::window::Input_event& input_event) -> bool
{
    const erhe::window::Xr_boolean_event& xr_boolean_event = input_event.u.xr_boolean_event;
    if (!m_has_cursor) {
        return false; // TODO Is this needed? Should not be.
    }
    auto& headset_view = *m_context.headset_view;
    const auto* headset = headset_view.get_headset();
    ERHE_VERIFY(headset != nullptr);

    ImGuiIO& io = m_imgui_context->IO;

    auto* trigger_click = headset->get_actions_right().trigger_click;
    auto* a_click       = headset->get_actions_right().a_click;
    if ((trigger_click != nullptr) && (xr_boolean_event.action == trigger_click)) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, xr_boolean_event.value);
        return true;
    }
    if ((a_click != nullptr) && (xr_boolean_event.action == a_click)) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, xr_boolean_event.value);
        return true;
    }
    auto* menu_click = headset->get_actions_left().menu_click;
    if ((menu_click != nullptr) && (xr_boolean_event.action == menu_click)) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, xr_boolean_event.value);
        return true;
    }

    // The thumbrest is not present for the second edition of the Oculus Touch (Rift S and Quest)
    auto* r_thumbrest_touch = headset->get_actions_right().thumbrest_touch;
    if ((r_thumbrest_touch != nullptr) && (xr_boolean_event.action == r_thumbrest_touch)) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, xr_boolean_event.value);
        return true;
    }

    auto* r_thumbstick_click = headset->get_actions_right().thumbstick_click;
    if ((r_thumbstick_click != nullptr) && (xr_boolean_event.action == r_thumbstick_click)) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, xr_boolean_event.value);
        return true;
    }
    return false;
}

auto Rendertarget_imgui_host::on_xr_float_event(const erhe::window::Input_event&) -> bool
{
    // TODO
    return false;
}

auto Rendertarget_imgui_host::on_xr_vector2f_event(const erhe::window::Input_event& input_event) -> bool
{
    if (!m_has_cursor) {
        return false; // TODO Is this needed? Should not be.
    }

    auto& headset_view = *m_context.headset_view;
    const auto* headset = headset_view.get_headset();
    ERHE_VERIFY(headset != nullptr);

    auto* r_thumbstick = headset->get_actions_right().thumbstick;
    if (r_thumbstick == nullptr) {
        return false;
    }
    const erhe::window::Xr_vector2f_event& xr_vector2f_event = input_event.u.xr_vector2f_event;
    if (xr_vector2f_event.action != r_thumbstick) {
        return false;
    }
    if (xr_vector2f_event.x != 0.0f || xr_vector2f_event.y != 0.0f) {
        erhe::window::Input_event mouse_wheel_event{
            .type = erhe::window::Input_event_type::mouse_wheel_event,
            .timestamp = input_event.timestamp,
            .u = {
                .mouse_wheel_event = {
                    .x             = xr_vector2f_event.x * 0.25f,
                    .y             = xr_vector2f_event.y * 0.25f,
                    .modifier_mask = 0
                }
            }
        };
        on_mouse_wheel_event(mouse_wheel_event);
    }
    return true;
}
#endif

auto Rendertarget_imgui_host::begin_imgui_frame() -> bool
{
    if (m_rendertarget_mesh == nullptr) {
        return false;
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_rendertarget_mesh->update_headset_hand_tracking();

    auto& headset_view = *m_context.headset_view;
#endif
    const auto pointer = m_rendertarget_mesh->get_pointer();
    ImGuiIO& io = m_imgui_context->IO;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (!m_context.OpenXR) // TODO Figure out better way to combine different input methods
#endif
    {
        if (pointer.has_value()) {
            if (!has_cursor()) {
                on_cursor_enter_event(
                    erhe::window::Input_event{
                        .type = erhe::window::Input_event_type::cursor_enter_event,
                        .timestamp = std::chrono::steady_clock::now(),
                        .u = {
                            .cursor_enter_event = {
                                .entered = true
                            }
                        }
                    }
                );
            }
            const auto position = pointer.value();
            if ((m_last_mouse_x != position.x) || (m_last_mouse_y != position.y)) {
                float dx = (m_last_mouse_x == -FLT_MAX) ? 0.0f : position.x - m_last_mouse_x;
                float dy = (m_last_mouse_y == -FLT_MAX) ? 0.0f : position.y - m_last_mouse_y;
                on_mouse_move_event(
                    erhe::window::Input_event{
                        .type = erhe::window::Input_event_type::mouse_move_event,
                        .timestamp = std::chrono::steady_clock::now(),
                        .u = {
                            .mouse_move_event = {
                                .x = position.x,
                                .y = position.y,
                                .dx = dx,
                                .dy = dy,
                                .modifier_mask = 0
                            }
                        }
                    }
                );
                m_last_mouse_x = position.x;
                m_last_mouse_y = position.y;
            }

            // TODO Is there better way to route mouse button and other events here?
            // Process input events from the context window
            std::vector<erhe::window::Input_event>& input_events = m_context.context_window->get_input_events();
            for (erhe::window::Input_event& input_event : input_events) {
                if (!input_event.handled) {
                    dispatch_input_event(input_event);
                }
            }

        } else {
            if (has_cursor()) {
                on_cursor_enter_event(
                    erhe::window::Input_event{
                        .type = erhe::window::Input_event_type::cursor_enter_event,
                        .timestamp = std::chrono::steady_clock::now(),
                        .u = {
                            .cursor_enter_event = {
                                .entered = false
                            }
                        }
                    }
                );
                m_last_mouse_x = -FLT_MAX;
                m_last_mouse_y = -FLT_MAX;
                on_mouse_move_event(
                    erhe::window::Input_event{
                        .type = erhe::window::Input_event_type::mouse_move_event,
                        .timestamp = std::chrono::steady_clock::now(),
                        .u = {
                            .mouse_move_event = {
                                .x = -FLT_MAX,
                                .y = -FLT_MAX,
                                .dx = 0.0f,
                                .dy = 0.0f,
                                .modifier_mask = 0
                            }
                        }
                    }
                );
            }
        }
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_context.OpenXR) {
        const auto* headset = headset_view.get_headset();
        ERHE_VERIFY(headset != nullptr);

        const auto* node = m_rendertarget_mesh->get_node();
        ERHE_VERIFY(node != nullptr);
        auto* left_aim_pose  = headset->get_actions_left().aim_pose;
        auto* right_aim_pose = headset->get_actions_right().aim_pose;
        const bool use_right = (right_aim_pose != nullptr) && (right_aim_pose->location.locationFlags != 0);

        auto* pose = use_right ? right_aim_pose : left_aim_pose;
        if ((pose != nullptr) && node->is_visible()) {
            const auto controller_orientation = glm::mat4_cast(pose->orientation);
            const auto controller_direction   = glm::vec3{controller_orientation * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};

            const glm::vec3 camera_offset = headset_view.get_camera_offset();
            glm::vec3 ray_origin = pose->position + camera_offset;
            const auto intersection = erhe::math::intersect_plane<float>(
                glm::vec3{node->direction_in_world()},
                glm::vec3{node->position_in_world()},
                ray_origin,
                controller_direction
            );

            bool mouse_has_position{false};
            // TODO Duplication with Rendertarget_mesh::update_pointer()
            if (intersection.has_value() && intersection.value() > 0.0f) {
                const auto world_position      = ray_origin + intersection.value() * controller_direction;
                const auto window_position_opt = m_rendertarget_mesh->world_to_window(world_position);
                if (window_position_opt.has_value()) {
                    if (!has_cursor()) {
                        on_cursor_enter_event(
                            erhe::window::Input_event{
                                .type = erhe::window::Input_event_type::cursor_enter_event,
                                .timestamp = std::chrono::steady_clock::now(),
                                .u = {
                                    .cursor_enter_event = {
                                        .entered = true
                                    }
                                }
                            }
                        );
                    }
                    mouse_has_position = true;
                    const auto position = window_position_opt.value();
                    if ((m_last_mouse_x != position.x) || (m_last_mouse_y != position.y)) {
                        float dx = (m_last_mouse_x == -FLT_MAX) ? 0.0f : position.x - m_last_mouse_x;
                        float dy = (m_last_mouse_y == -FLT_MAX) ? 0.0f : position.y - m_last_mouse_y;
                        on_mouse_move_event(
                            erhe::window::Input_event{
                                .type = erhe::window::Input_event_type::mouse_move_event,
                                .timestamp = std::chrono::steady_clock::now(),
                                .u = {
                                    .mouse_move_event = {
                                        .x = position.x,
                                        .y = position.y,
                                        .dx = dx,
                                        .dy = dy,
                                        .modifier_mask = 0
                                    }
                                }
                            }
                        );
                        m_last_mouse_x = position.x;
                        m_last_mouse_y = position.y;
                    }

                    // Process input events from the context window
                    // Here we are interested in the XR input events
                    std::vector<erhe::window::Input_event>& input_events = m_context.context_window->get_input_events();
                    //if (input_events.empty()) {
                    //    SPDLOG_LOGGER_TRACE(log_frame, "Rendertarget_imgui_host - no input events");
                    //}
                    for (erhe::window::Input_event& input_event : input_events) {
                        if (!input_event.handled) {
                            dispatch_input_event(input_event);
                            SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_host processed {} - {}", input_event.describe(), input_event.handled ? "handled" : "stays unhandled");
                            SPDLOG_LOGGER_TRACE(log_input,                      "Rendertarget_imgui_host processed {} - {}", input_event.describe(), input_event.handled ? "handled" : "stays unhandled");
                        } else {
                            SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_host skipped already handled {}", input_event.describe());
                            SPDLOG_LOGGER_TRACE(log_input,                      "Rendertarget_imgui_host skipped already handled {}", input_event.describe());
                        }
                    }

                }
            }
            if (!mouse_has_position) {
                if (has_cursor()) {
                    on_cursor_enter_event(
                        erhe::window::Input_event{
                            .type = erhe::window::Input_event_type::cursor_enter_event,
                            .timestamp = std::chrono::steady_clock::now(),
                            .u = {
                                .cursor_enter_event = {
                                    .entered = false
                                }
                            }
                        }
                    );
                    m_last_mouse_x = -FLT_MAX;
                    m_last_mouse_y = -FLT_MAX;
                    on_mouse_move_event(
                        erhe::window::Input_event{
                            .type = erhe::window::Input_event_type::mouse_move_event,
                            .timestamp = std::chrono::steady_clock::now(),
                            .u = {
                                .mouse_move_event = {
                                    .x = -FLT_MAX,
                                    .y = -FLT_MAX,
                                    .dx = 0.0f,
                                    .dy = 0.0f,
                                    .modifier_mask = 0
                                }
                            }
                        }
                    );
                }
            }
        }
    }
#endif

    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

    ImGui::NewFrame();
    ////ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_begin_callback) {
        m_begin_callback(*this);
    }

    return true;
}

void Rendertarget_imgui_host::end_imgui_frame()
{
    //SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_host::end_imgui_frame()");

    ImGui::EndFrame();
    ImGui::Render();
}

void Rendertarget_imgui_host::set_clear_color(const glm::vec4& value)
{
    m_clear_color = value;
}

void Rendertarget_imgui_host::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_host::execute_rendergraph_node()");

    if (!m_enabled) {
        return;
    }

    erhe::imgui::Imgui_host& imgui_host = *this;
    erhe::imgui::Scoped_imgui_context imgui_context{imgui_host};

    m_rendertarget_mesh->bind();
    m_rendertarget_mesh->clear(m_clear_color);
    m_context.imgui_renderer->render_draw_data();
    m_rendertarget_mesh->render_done(m_context);
}

auto Rendertarget_imgui_host::get_consumer_input_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_rendertarget_mesh->texture();
}

auto Rendertarget_imgui_host::get_producer_output_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_rendertarget_mesh->texture();
}

auto Rendertarget_imgui_host::get_consumer_input_framebuffer(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    return m_rendertarget_mesh->framebuffer();
}

auto Rendertarget_imgui_host::get_consumer_input_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = static_cast<int>(m_rendertarget_mesh->width()),
        .height = static_cast<int>(m_rendertarget_mesh->height())
        // .reverse_depth = false // unused
    };
}

auto Rendertarget_imgui_host::get_producer_output_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = static_cast<int>(m_rendertarget_mesh->width()),
        .height = static_cast<int>(m_rendertarget_mesh->height())
        // .reverse_depth = false // unused
    };
}

}  // namespace editor



#if 0
    if (m_hand_tracker) {
        m_rendertarget_node->update_headset(headset_view);
        const auto& pointer_finger_opt = m_rendertarget_node->get_pointer_finger();
        if (pointer_finger_opt.has_value()) {
            const auto& pointer_finger           = pointer_finger_opt.value();
            headset_view.add_finger_input(pointer_finger);
            const auto  finger_world_position    = pointer_finger.finger_point;
            const auto  viewport_world_position  = pointer_finger.point;
            //const float distance                 = glm::distance(finger_world_position, viewport_world_position);
            const auto  window_position_opt      = m_rendertarget_node->world_to_window(viewport_world_position);

            if (window_position_opt.has_value()) {
                if (!has_cursor()) {
                    on_cursor_enter(1);
                }
                const auto position = window_position_opt.value();
                if (
                    (m_last_mouse_x != position.x) ||
                    (m_last_mouse_y != position.y)
                ) {
                    m_last_mouse_x = position.x;
                    m_last_mouse_y = position.y;
                    on_mouse_move(position.x, position.y);
                }
            } else {
                if (has_cursor()) {
                    on_cursor_enter(0);
                    m_last_mouse_x = -FLT_MAX;
                    m_last_mouse_y = -FLT_MAX;
                    on_mouse_move(-FLT_MAX, -FLT_MAX);
                }
            }

            ////const float finger_press_threshold = headset_view.finger_to_viewport_distance_threshold();
            ////if ((distance < finger_press_threshold * 0.98f) && (!m_last_mouse_finger)) {
            ////    m_last_mouse_finger = true;
            ////    ImGuiIO& io = m_imgui_context->IO;
            ////    io.AddMouseButtonEvent(0, true);
            ////}
            ////if ((distance > finger_press_threshold * 1.02f) && (m_last_mouse_finger)) {
            ////    m_last_mouse_finger = false;
            ////    ImGuiIO& io = m_imgui_context->IO;
            ////    io.AddMouseButtonEvent(0, false);
            ////}
        }
        const bool finger_trigger = m_rendertarget_node->get_finger_trigger();

        if (finger_trigger && (!m_last_mouse_finger)) {
            m_last_mouse_finger = true;
            ImGuiIO& io = m_imgui_context->IO;
            io.AddMouseButtonEvent(0, true);
        }
        if (!finger_trigger && (m_last_mouse_finger)) {
            m_last_mouse_finger = false;
            ImGuiIO& io = m_imgui_context->IO;
            io.AddMouseButtonEvent(0, false);
        }
    }
#endif
