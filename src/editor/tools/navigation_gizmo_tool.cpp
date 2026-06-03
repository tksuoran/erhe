#include "tools/navigation_gizmo_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "time.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/gtx/norm.hpp>

namespace editor {

#pragma region Navigation_gizmo_drag_command
Navigation_gizmo_drag_command::Navigation_gizmo_drag_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Navigation_gizmo.drag"}
    , m_context{context}
{
}

void Navigation_gizmo_drag_command::try_ready()
{
    if (m_context.navigation_gizmo_tool->on_drag_ready()) {
        set_ready();
    }
}

auto Navigation_gizmo_drag_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    // Press over the gizmo: claim the gesture so no other viewport command can start.
    // (Reached on button down because the binding uses call_on_button_down_without_motion.)
    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
        m_context.navigation_gizmo_tool->on_drag_begin();
        return true;
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    return m_context.navigation_gizmo_tool->on_drag(input.variant.vector2.relative_value);
}

void Navigation_gizmo_drag_command::on_inactive()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        m_context.navigation_gizmo_tool->on_drag_end();
    }
}
#pragma endregion Navigation_gizmo_drag_command

Navigation_gizmo_tool::Navigation_gizmo_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools
)
    : Tool          {context, tools, Tool_flags::background}
    , m_drag_command{commands, context}
{
    set_base_priority(c_priority);
    set_description  ("Navigation gizmo");

    commands.register_command          (&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::window::Mouse_button_left, true);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );

    m_drag_command.set_host(this);
}

auto Navigation_gizmo_tool::get_target_scene_view() -> Viewport_scene_view*
{
    Scene_view* scene_view = get_hover_scene_view();
    return (scene_view != nullptr) ? scene_view->as_viewport_scene_view() : nullptr;
}

auto Navigation_gizmo_tool::on_drag_ready() -> bool
{
    Viewport_scene_view* viewport_scene_view = get_target_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    if (!viewport_scene_view->get_show_navigation_gizmo()) {
        return false;
    }

    ImViewGuizmo::Context&     gizmo  = viewport_scene_view->get_navigation_gizmo();
    const ImViewGuizmo::Region region = gizmo.get_region();
    if (region == ImViewGuizmo::Region::none) {
        return false;
    }

    m_drag_region = region;
    m_drag_axis   = gizmo.get_hovered_axis();
    m_drag_offset = glm::vec2{0.0f, 0.0f};
    return true;
}

void Navigation_gizmo_tool::on_drag_begin()
{
    Viewport_scene_view* viewport_scene_view = get_target_scene_view();
    if (viewport_scene_view == nullptr) {
        return;
    }
    viewport_scene_view->get_navigation_gizmo().begin_tool(m_drag_region);
}

auto Navigation_gizmo_tool::on_drag(glm::vec2 relative) -> bool
{
    Viewport_scene_view* viewport_scene_view = get_target_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    m_drag_offset += relative;

    const std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view->get_camera();
    erhe::scene::Node* node = camera ? camera->get_node() : nullptr;
    if (node == nullptr) {
        return true; // The gesture is ours; there is just nothing to move.
    }

    erhe::scene::Trs_transform transform   = node->world_from_node_transform();
    glm::quat                  rotation    = transform.get_rotation();
    glm::vec3                  translation = transform.get_translation();
    if (viewport_scene_view->get_navigation_gizmo().drag(translation, rotation, relative)) {
        transform.set_translation(translation);
        transform.set_rotation   (rotation);
        node->set_world_from_node(transform);
        m_context.app_message_bus->node_touched.send_message(
            Node_touched_message{
                .source = Node_touch_source::navigation_gizmo,
                .node   = node
            }
        );
    }
    return true;
}

void Navigation_gizmo_tool::on_drag_end()
{
    Viewport_scene_view* viewport_scene_view = get_target_scene_view();
    if (viewport_scene_view != nullptr) {
        ImViewGuizmo::Context& gizmo = viewport_scene_view->get_navigation_gizmo();

        // A click (pointer barely moved) on an axis handle snaps the view to that axis.
        // The threshold matches ImGui's default mouse drag threshold so small click jitter
        // still snaps while a deliberate drag does not.
        constexpr float drag_threshold = 6.0f;
        if ((glm::length(m_drag_offset) < drag_threshold) && (m_drag_axis >= 0) && (m_drag_axis <= 5)) {
            const std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view->get_camera();
            erhe::scene::Node* node = camera ? camera->get_node() : nullptr;
            if (node != nullptr) {
                erhe::scene::Trs_transform transform   = node->world_from_node_transform();
                glm::quat                  rotation    = transform.get_rotation();
                glm::vec3                  translation = transform.get_translation();
                const int64_t              time_ns     = m_context.time->get_host_system_time_ns();
                const erhe::math::Aabb&    framed_aabb = m_context.fly_camera_tool->get_framed_aabb();
                const float focus_distance = framed_aabb.is_valid()
                    ? glm::distance(framed_aabb.center(), translation)
                    : 2.0f;
                if (gizmo.snap(translation, rotation, m_drag_axis, time_ns, focus_distance)) {
                    transform.set_translation(translation);
                    transform.set_rotation   (rotation);
                    node->set_world_from_node(transform);
                    m_context.app_message_bus->node_touched.send_message(
                        Node_touched_message{
                            .source = Node_touch_source::navigation_gizmo,
                            .node   = node
                        }
                    );
                }
            }
        }
        gizmo.end_tool();
    }

    m_drag_region = ImViewGuizmo::Region::none;
    m_drag_axis   = -1;
    m_drag_offset = glm::vec2{0.0f, 0.0f};
}

}
