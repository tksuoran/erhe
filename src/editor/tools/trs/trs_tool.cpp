#include "tools/trs/trs_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp" // need to be able to pass to visualization
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs/move_tool.hpp"
#include "tools/trs/rotate_tool.hpp"
#include "windows/operations.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::normalize;
using glm::cross;
using glm::dot;
using glm::distance;
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

#pragma region Commands

Trs_tool_drag_command::Trs_tool_drag_command()
    : Command{"Trs_tool.drag"}
{
}

void Trs_tool_drag_command::try_ready()
{
    if (g_trs_tool->on_drag_ready())
    {
        set_ready();
    }
}

auto Trs_tool_drag_command::try_call() -> bool
{
    if (get_command_state() == erhe::application::State::Ready)
    {
        set_active();
    }

    if (get_command_state() != erhe::application::State::Active)
    {
        return false; // We might be ready, but not consuming event yet
    }

    const bool still_active = g_trs_tool->on_drag();
    if (!still_active)
    {
        set_inactive();
    }
    return still_active;
}

void Trs_tool_drag_command::on_inactive()
{
    log_trs_tool->trace("TRS on_inactive");

    if (get_command_state() != erhe::application::State::Inactive)
    {
        g_trs_tool->end_drag();
    }
}

#pragma endregion Commands

Trs_tool* g_trs_tool{nullptr};

Trs_tool::Trs_tool()
    : Imgui_window                  {c_title}
    , erhe::components::Component   {c_type_name}
    , m_drag_redirect_update_command{m_drag_command}
    , m_drag_enable_command         {m_drag_redirect_update_command}
{
}

Trs_tool::~Trs_tool() noexcept
{
    ERHE_VERIFY(g_trs_tool == nullptr);
}

void Trs_tool::deinitialize_component()
{
    ERHE_VERIFY(g_trs_tool == this);
    m_drag_command.set_host(nullptr);
    m_target_node.reset();
    m_visualization.reset();
    m_tool_node.reset();
    m_original_motion_mode.reset();

    g_trs_tool = nullptr;
}

void Trs_tool::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<Editor_message_bus>();
    require<Editor_scenes >();
    require<Mesh_memory   >();
    require<Selection_tool>();
    require<Tools         >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view      >();
#endif
}

void Trs_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_trs_tool == nullptr);
    g_trs_tool = this; // visualizations needs config

    auto ini = erhe::application::get_ini("erhe.ini", "trs_tool");
    ini->get("scale",          config.scale);
    ini->get("show_translate", config.show_translate);
    ini->get("show_rotate",    config.show_rotate);

    const erhe::application::Scoped_gl_context gl_context;

    const auto& tool_scene_root = g_tools->get_tool_scene_root();
    if (!tool_scene_root)
    {
        return;
    }
    m_visualization.emplace(*this);

    set_base_priority(c_priority);
    set_description  (c_title);

    g_tools->register_tool(this);

    auto& commands = *erhe::application::g_commands;
    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_left, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr)
    {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.trigger_click, erhe::application::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.a_click,       erhe::application::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#endif

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );

    m_drag_command.set_host(this);
}

void Trs_tool::on_message(Editor_message& message)
{
    Tool::on_message(message);

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection))
    {
        set_node(g_selection_tool->get_first_selected_node());
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_mesh))
    {
        tool_hover();
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view))
    {
        update_for_view(message.scene_view);
    }
}

[[nodiscard]] auto Trs_tool::is_trs_active() const -> bool
{
    return m_active_handle != Handle::e_handle_none;
}

void Trs_tool::set_translate(const bool enabled)
{
    m_visualization->set_translate(enabled);
}

void Trs_tool::set_rotate(const bool enabled)
{
    m_visualization->set_rotate(enabled);
}

auto Trs_tool::get_target_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_target_node.lock();
}

[[nodiscard]] auto Trs_tool::get_target_node_physics() const -> std::shared_ptr<Node_physics>
{
    const auto target_node = get_target_node();
    if (!target_node)
    {
        return {};
    }
    return get_node_physics(target_node.get());
}

void Trs_tool::set_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    if (node == m_target_node.lock())
    {
        return;
    }

    m_target_node = node;
    m_visualization->set_target(m_target_node.lock());
}

void Trs_tool::touch()
{
    if (!m_touched)
    {
        log_trs_tool->trace("TRS touch - not touched");
        begin_move();
    }
}

void Trs_tool::begin_move()
{
    m_touched = true;
    const auto node_physics = get_target_node_physics();
    auto* const rigid_body = node_physics
        ? node_physics->rigid_body()
        : nullptr;

    if (rigid_body == nullptr)
    {
        log_trs_tool->trace("TRS begin_move - no rigid body");
        return;
    }

    log_trs_tool->trace("TRS begin_move");

    m_original_motion_mode = rigid_body->get_motion_mode();
    rigid_body->set_motion_mode(m_motion_mode);
    rigid_body->begin_move();
}

void Trs_tool::end_move()
{
    const auto node_physics = get_target_node_physics();
    if (
        node_physics &&
        node_physics->rigid_body()
    )
    {
        ERHE_VERIFY(m_original_motion_mode.has_value());
        log_trs_tool->trace("S restoring old physics node");
        auto* const rigid_body = node_physics->rigid_body();
        rigid_body->set_motion_mode     (m_original_motion_mode.value());
        rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        rigid_body->end_move            ();
        node_physics->handle_node_transform_update();
        m_original_motion_mode.reset();
    }

    m_touched = false;
    log_trs_tool->trace("TRS end_move");
}

void Trs_tool::set_local(const bool local)
{
    m_visualization->set_local(local);
}

void Trs_tool::viewport_toolbar(bool& hovered)
{
    if (g_icon_set != nullptr)
    {
        m_visualization->viewport_toolbar(hovered);
    }
}

void Trs_tool::imgui()
{
    m_visualization->imgui();
#if defined(ERHE_GUI_LIBRARY_IMGUI)

    ImGui::Checkbox("Cast Rays", &m_cast_rays);

    ImGui::Text("Hover handle: %s", c_str(m_hover_handle));
    ImGui::Text("Active handle: %s", c_str(m_active_handle));

    {
        ImGui::Checkbox("Translate Snap Enable", &m_translate_snap_enable);
        const float translate_snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
        const char* translate_snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
        erhe::application::make_combo(
            "Translate Snap",
            m_translate_snap_index,
            translate_snap_items,
            IM_ARRAYSIZE(translate_snap_items)
        );
        if (
            (m_translate_snap_index >= 0) &&
            (m_translate_snap_index < IM_ARRAYSIZE(translate_snap_values))
        )
        {
            m_translate_snap = translate_snap_values[m_translate_snap_index];
        }
    }

    ImGui::Separator();

    {
        ImGui::Checkbox("Rotate Snap Enable", &m_rotate_snap_enable);
        const float rotate_snap_values[] = {  5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
        const char* rotate_snap_items [] = { "5",  "10",  "15",  "20",  "30",  "45",  "60",  "90" };
        erhe::application::make_combo(
            "Rotate Snap",
            m_rotate_snap_index,
            rotate_snap_items,
            IM_ARRAYSIZE(rotate_snap_items)
        );
        if (
            (m_rotate_snap_index >= 0) &&
            (m_rotate_snap_index < IM_ARRAYSIZE(rotate_snap_values))
        )
        {
            m_rotate_snap = rotate_snap_values[m_rotate_snap_index];
        }
    }
#endif
}

void Trs_tool::tool_hover()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        m_hover_handle = Handle::e_handle_none;
        return;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || !tool.mesh)
    {
        if (m_hover_handle != Handle::e_handle_none)
        {
            m_hover_handle = Handle::e_handle_none;
        }
        return;
    }

    const auto new_handle = get_handle(tool.mesh.get());
    if (m_hover_handle == new_handle)
    {
        return;
    }
    m_hover_handle = get_handle(tool.mesh.get());
}

auto Trs_tool::on_drag() -> bool
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        log_trs_tool->trace("TRS no scene view");
        end_drag();
        return false;
    }
    Viewport_window* viewport_window = scene_view->as_viewport_window();

    auto handle_type = get_handle_type(m_active_handle);
    switch (handle_type)
    {
        //using enum Handle_type;
        case Handle_type::e_handle_type_translate_axis:
        {
            if (viewport_window != nullptr)
            {
                update_axis_translate_2d(viewport_window);
            }
            else
            {
                update_axis_translate_3d(scene_view);
            }
            return true;
        }

        case Handle_type::e_handle_type_translate_plane:
        {
            if (viewport_window != nullptr)
            {
                update_plane_translate_2d(viewport_window);
            }
            else
            {
                update_plane_translate_3d(scene_view);
            }
            return true;
        }

        case Handle_type::e_handle_type_rotate:
        {
            update_rotate(scene_view);
            return true;
        }

        case Handle_type::e_handle_type_none:
        default:
        {
            return false;
        }
    }
}

auto Trs_tool::on_drag_ready() -> bool
{
    log_trs_tool->trace("TRS on_drag_ready");

    m_active_handle = m_hover_handle;

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        log_trs_tool->trace("drag not possible - scene_view == nullptr");
        return false;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || !tool.mesh)
    {
        log_trs_tool->trace("drag not possible - !tool.valid || !tool.mesh");
        return false;
    }

    const auto target_node = m_target_node.lock();
    if (
        (m_active_handle == Handle::e_handle_none) ||
        (!target_node)
    )
    {
        log_trs_tool->trace("drag not possible - no handle, or no root");
        return false;
    }

    const auto camera = scene_view->get_camera();
    if (!camera)
    {
        log_trs_tool->trace("drag not possible - no camera");
        return false;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr)
    {
        log_trs_tool->trace("drag not possible - no camera node");
        return false;
    }

    m_drag.initial_position_in_world = tool.position.value();
    m_drag.initial_world_from_local  = target_node->world_from_node();
    m_drag.initial_local_from_world  = target_node->node_from_world();
    m_drag.initial_distance          = glm::distance(
        glm::vec3{camera_node->position_in_world()},
        tool.position.value()
    );
    if (target_node)
    {
        m_drag.initial_parent_from_node_transform = target_node->parent_from_node_transform();
    }

    // For rotation
    if (is_rotate_active() && target_node)
    {
        const bool world        = !m_visualization->get_local();
        const vec3 n            = get_plane_normal(world);
        const vec3 side         = get_plane_side  (world);
        const vec3 center       = target_node->position_in_world();
        const auto intersection = project_pointer_to_plane(scene_view, n, center);
        if (intersection.has_value())
        {
            const vec3 direction = normalize(intersection.value() - center);
            m_rotation = Rotation_context{
                .normal               = n,
                .reference_direction  = direction,
                .center_of_rotation   = center,
                .start_rotation_angle = erhe::toolkit::angle_of_rotation<float>(direction, n, side),
                .world_from_node      = target_node->world_from_node_transform()
            };
        }
        else
        {
            log_trs_tool->trace("drag not possible - no intersection");
            return false;
        }
    }

    log_trs_tool->trace("drag has been activated");
    return true;
}

void Trs_tool::end_drag()
{
    log_trs_tool->trace("ending drag");

    m_active_handle                  = Handle::e_handle_none;
    m_drag.initial_position_in_world = vec3{0.0f};
    m_drag.initial_world_from_local  = mat4{1.0f};
    m_drag.initial_distance          = 0.0;

    const auto target_node = m_target_node.lock();
    if (m_touched && target_node)
    {
        log_trs_tool->trace("m_touched && m_target_node -> creating transform operation");

        g_operation_stack->push(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = target_node,
                    .parent_from_node_before = m_drag.initial_parent_from_node_transform,
                    .parent_from_node_after  = target_node->parent_from_node_transform()
                }
            )
        );
        end_move();
    }

    log_trs_tool->trace("drag ended");
}

auto Trs_tool::get_axis_direction() const -> vec3
{
    const bool local = m_visualization->get_local();
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_translate_x:  return local ? m_drag.initial_world_from_local[0] : vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_translate_y:  return local ? m_drag.initial_world_from_local[1] : vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_translate_z:  return local ? m_drag.initial_world_from_local[2] : vec3{0.0f, 0.0f, 1.0f};
        case Handle::e_handle_translate_yz: return local ? m_drag.initial_world_from_local[0] : vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_translate_xz: return local ? m_drag.initial_world_from_local[1] : vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_translate_xy: return local ? m_drag.initial_world_from_local[2] : vec3{0.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_x:     return local ? m_drag.initial_world_from_local[0] : vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y:     return local ? m_drag.initial_world_from_local[1] : vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_rotate_z:     return local ? m_drag.initial_world_from_local[2] : vec3{0.0f, 0.0f, 1.0f};
        default:
        {
            ERHE_FATAL("bad axis");
            break;
        }
    }
}

void Trs_tool::update_axis_translate_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION

    if (viewport_window == nullptr)
    {
        return;
    }
    const auto position_in_viewport_opt = viewport_window->get_position_in_viewport();
    if (!position_in_viewport_opt.has_value())
    {
        return;
    }

    const vec3 drag_world_direction = get_axis_direction();
    const vec3 P0        = m_drag.initial_position_in_world - drag_world_direction;
    const vec3 P1        = m_drag.initial_position_in_world + drag_world_direction;
    const auto ss_P0_opt = viewport_window->project_to_viewport(P0);
    const auto ss_P1_opt = viewport_window->project_to_viewport(P1);
    if (
        !ss_P0_opt.has_value() ||
        !ss_P1_opt.has_value()
    )
    {
        return;
    }
    const vec3 ss_P0      = ss_P0_opt.value();
    const vec3 ss_P1      = ss_P1_opt.value();
    const auto ss_closest = erhe::toolkit::closest_point<float>(
        vec2{ss_P0},
        vec2{ss_P1},
        vec2{position_in_viewport_opt.value()}
    );

    if (ss_closest.has_value())
    {
        const auto R0_opt = viewport_window->unproject_to_world(vec3{ss_closest.value(), 0.0f});
        const auto R1_opt = viewport_window->unproject_to_world(vec3{ss_closest.value(), 1.0f});
        if (R0_opt.has_value() && R1_opt.has_value())
        {
            const auto R0 = R0_opt.value();
            const auto R1 = R1_opt.value();
            const auto closest_points_r = erhe::toolkit::closest_points<float>(P0, P1, R0, R1);
            if (closest_points_r.has_value())
            {
                update_axis_translate_final(closest_points_r.value().P);
            }
        }
    }
    else
    {
        const auto Q0_opt = viewport_window->position_in_world_viewport_depth(1.0);
        const auto Q1_opt = viewport_window->position_in_world_viewport_depth(0.0);
        if (Q0_opt.has_value() && Q1_opt.has_value())
        {
            const auto Q0 = Q0_opt.value();
            const auto Q1 = Q1_opt.value();
            const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
            if (closest_points_q.has_value())
            {
                update_axis_translate_final(closest_points_q.value().P);
            }
        }
    }
}

void Trs_tool::update_axis_translate_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    if (scene_view == nullptr)
    {
        log_trs_tool->trace("scene_view == nullptr");
        return;
    }

    const vec3 drag_world_direction = get_axis_direction();
    const vec3 P0 = m_drag.initial_position_in_world - drag_world_direction;
    const vec3 P1 = m_drag.initial_position_in_world + drag_world_direction;
    const auto Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (Q_origin_opt.has_value() && Q_direction_opt.has_value())
    {
        const auto Q0 = Q_origin_opt.value();
        const auto Q1 = Q0 + Q_direction_opt.value();
        const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
        if (closest_points_q.has_value())
        {
            update_axis_translate_final(closest_points_q.value().P);
        }
        else
        {
            log_trs_tool->trace("!closest_points_q.has_value()");
        }
    }
    else
    {
        log_trs_tool->trace("! (Q_origin_opt.has_value() && Q_direction_opt.has_value())");
    }
}

void Trs_tool::update_axis_translate_final(const vec3 drag_position_in_world)
{
    const vec3 translation_vector  = drag_position_in_world - m_drag.initial_position_in_world;
    const vec3 snapped_translation = snap_translate(translation_vector);
    const mat4 translation         = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 world_from_node     = translation * m_drag.initial_world_from_local;

    touch();
    set_node_world_transform(world_from_node);
    update_transforms();
}

auto Trs_tool::is_x_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_x ) ||
        (m_active_handle == Handle::e_handle_translate_xy) ||
        (m_active_handle == Handle::e_handle_translate_xz);
}

auto Trs_tool::is_y_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_y ) ||
        (m_active_handle == Handle::e_handle_translate_xy) ||
        (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_z_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_z ) ||
        (m_active_handle == Handle::e_handle_translate_xz) ||
        (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_rotate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_rotate_x) ||
        (m_active_handle == Handle::e_handle_rotate_y) ||
        (m_active_handle == Handle::e_handle_rotate_z);
}

auto Trs_tool::snap_translate(const vec3 in_translation) const -> vec3
{
    if (!m_translate_snap_enable)
    {
        return in_translation;
    }

    const float in_x = in_translation.x;
    const float in_y = in_translation.y;
    const float in_z = in_translation.z;
    const float snap = m_translate_snap;
    const float x    = is_x_translate_active() ? std::floor((in_x + snap * 0.5f) / snap) * snap : in_x;
    const float y    = is_y_translate_active() ? std::floor((in_y + snap * 0.5f) / snap) * snap : in_y;
    const float z    = is_z_translate_active() ? std::floor((in_z + snap * 0.5f) / snap) * snap : in_z;

    return vec3{x, y, z};
}

auto Trs_tool::snap_rotate(const float angle_radians) const -> float
{
    if (!m_rotate_snap_enable)
    {
        return angle_radians;
    }

    const float snap = glm::radians<float>(m_rotate_snap);
    return std::floor((angle_radians + snap * 0.5f) / snap) * snap;
}

auto Trs_tool::get_plane_normal(const bool world) const -> vec3
{
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x:
        case Handle::e_handle_translate_yz:
        {
            return world
                ? vec3{1.0f, 0.0f, 0.0f}
                : m_drag.initial_world_from_local[0];
        }

        case Handle::e_handle_rotate_y:
        case Handle::e_handle_translate_xz:
        {
            return world
                ? vec3{0.0f, 1.0f, 0.0f}
                : m_drag.initial_world_from_local[1];
        }

        case Handle::e_handle_rotate_z:
        case Handle::e_handle_translate_xy:
        {
            return world
                ? vec3{0.0f, 0.0f, 1.0f}
                : m_drag.initial_world_from_local[2];
        }

        default:
        {
            ERHE_FATAL("bad handle for plane %04x", static_cast<unsigned int>(m_active_handle));
            break;
        }
    }
}

auto Trs_tool::get_plane_side(const bool world) const -> vec3
{
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x:
        case Handle::e_handle_translate_yz: return world ? vec3{0.0f, 1.0f, 0.0f} : m_drag.initial_world_from_local[1];
        case Handle::e_handle_rotate_y:
        case Handle::e_handle_translate_xz: return world ? vec3{0.0f, 0.0f, 1.0f} : m_drag.initial_world_from_local[2];
        case Handle::e_handle_rotate_z:
        case Handle::e_handle_translate_xy: return world ? vec3{1.0f, 0.0f, 0.0f} : m_drag.initial_world_from_local[0];
        default:
            ERHE_FATAL("bad handle for plane %04x", static_cast<unsigned int>(m_active_handle));
            break;
    }
}

void Trs_tool::update_plane_translate_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION

    if (viewport_window == nullptr)
    {
        return;
    }

    const vec3 p0      = m_drag.initial_position_in_world;
    const vec3 world_n = get_plane_normal(!m_visualization->get_local());
    const auto Q0_opt  = viewport_window->position_in_world_viewport_depth(1.0);
    const auto Q1_opt  = viewport_window->position_in_world_viewport_depth(0.0);
    if (
        !Q0_opt.has_value() ||
        !Q1_opt.has_value()
    )
    {
        return;
    }
    const vec3 Q0 = Q0_opt.value();
    const vec3 Q1 = Q1_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(world_n, p0, Q0, v);
    if (!intersection.has_value())
    {
        return;
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    const vec3 translation_vector               = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    const vec3 snapped_translation              = snap_translate(translation_vector);
    const mat4 translation                      = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 world_from_node                  = translation * m_drag.initial_world_from_local;

    touch();
    set_node_world_transform(world_from_node);
    update_transforms();
}

void Trs_tool::update_plane_translate_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    if (scene_view == nullptr)
    {
        return;
    }

    const vec3 p0              = m_drag.initial_position_in_world;
    const vec3 world_n         = get_plane_normal(!m_visualization->get_local());
    const auto Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !Q_origin_opt.has_value() ||
        !Q_direction_opt.has_value()
    )
    {
        return;
    }
    const vec3 Q0 = Q_origin_opt.value();
    const vec3 Q1 = Q0 + Q_direction_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(world_n, p0, Q0, v);
    if (!intersection.has_value())
    {
        return;
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    const vec3 translation_vector               = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    const vec3 snapped_translation              = snap_translate(translation_vector);
    const mat4 translation                      = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 world_from_node                  = translation * m_drag.initial_world_from_local;

    touch();
    set_node_world_transform(world_from_node);
    update_transforms();
}

auto Trs_tool::offset_plane_origo(const Handle handle, const vec3 p) const -> vec3
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x: return vec3{ p.x, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y: return vec3{0.0f,  p.y, 0.0f};
        case Handle::e_handle_rotate_z: return vec3{0.0f, 0.0f,  p.z};
        default:
            ERHE_FATAL("bad handle for rotate %04x", static_cast<unsigned int>(handle));
            break;
    }
}

auto Trs_tool::project_to_offset_plane(
    const Handle handle,
    const vec3   P,
    const vec3   Q
) const -> vec3
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x: return vec3{P.x, Q.y, Q.z};
        case Handle::e_handle_rotate_y: return vec3{Q.x, P.y, Q.z};
        case Handle::e_handle_rotate_z: return vec3{Q.x, Q.y, P.z};
        default:
            ERHE_FATAL("bad handle for rotate %04x", static_cast<unsigned int>(handle));
            break;
    }
}

auto Trs_tool::project_pointer_to_plane(
    Scene_view* scene_view,
    const vec3  n,
    const vec3  p
) -> std::optional<vec3>
{
    if (scene_view == nullptr)
    {
        return {};
    }

    const auto origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !origin_opt.has_value() ||
        !direction_opt.has_value()
    )
    {
        return {};
    }

    const vec3 q0           = origin_opt.value();
    const vec3 v            = direction_opt.value();
    const auto intersection = erhe::toolkit::intersect_plane<float>(n, p, q0, v);
    if (intersection.has_value())
    {
        return q0 + intersection.value() * v;
    }
    return {};
}

void Trs_tool::update_rotate(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    // log_trs_tool->trace("update_rotate()");

    const auto target_node = m_target_node.lock();
    if (!target_node)
    {
        return;
    }

    //constexpr float c_parallel_threshold = 0.2f;
    //const vec3  V0      = vec3{root()->position_in_world()} - vec3{camera->position_in_world()};
    //const vec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0});
    //const float v_dot_n = dot(V, m_rotation.normal);
    bool ready_to_rotate{false};
    //log_trs_tool->trace("R: {} @ {}", root()->name(), root()->position_in_world());
    //log_trs_tool->trace("C: {} @ {}", camera->name(), camera->position_in_world());
    //log_trs_tool->trace("V: {}", vec3{V});
    //log_trs_tool->trace("N: {}", vec3{m_rotation.normal});
    //log_trs_tool->trace("V.N = {}", v_dot_n);
    //if (std::abs(v_dot_n) > c_parallel_threshold) TODO
    {
        ready_to_rotate = update_rotate_circle_around(scene_view);
    }

    if (!ready_to_rotate)
    {
        ready_to_rotate = update_rotate_parallel(scene_view);
    }
    if (ready_to_rotate)
    {
        update_rotate_final();
    }
}

auto Trs_tool::update_rotate_circle_around(Scene_view* scene_view) -> bool
{
    m_rotation.intersection = project_pointer_to_plane(
        scene_view,
        m_rotation.normal,
        m_rotation.center_of_rotation
    );
    return m_rotation.intersection.has_value();
}

auto Trs_tool::update_rotate_parallel(Scene_view* scene_view) -> bool
{
    if (scene_view == nullptr)
    {
        return false;
    }
    const auto p_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto p_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!p_origin_opt.has_value() || !p_direction_opt.has_value())
    {
        return false;
    }

    const auto p0        = p_origin_opt.value();
    const auto direction = p_direction_opt.value();
    const auto q0        = p0 + m_drag.initial_distance * direction;

    m_rotation.intersection = project_to_offset_plane(
        m_active_handle,
        m_rotation.center_of_rotation,
        q0
    );
    return true;
}

void Trs_tool::update_rotate_final()
{
    Expects(m_rotation.intersection.has_value());

    const vec3  q_                     = normalize                               (m_rotation.intersection.value() - m_rotation.center_of_rotation);
    const float angle                  = erhe::toolkit::angle_of_rotation<float> (q_, m_rotation.normal, m_rotation.reference_direction);
    const float snapped_angle          = snap_rotate                             (angle);
    const vec3  rotation_axis_in_world = get_axis_direction                      ();
    const mat4  rotation               = erhe::toolkit::create_rotation<float>   (snapped_angle, rotation_axis_in_world);
    const mat4  translate              = erhe::toolkit::create_translation<float>(vec3{-m_rotation.center_of_rotation});
    const mat4  untranslate            = erhe::toolkit::create_translation<float>(vec3{ m_rotation.center_of_rotation});
    const mat4  world_from_node        = untranslate * rotation * translate * m_drag.initial_world_from_local;

    m_rotation.current_angle = angle;

    touch();
    set_node_world_transform(world_from_node);
    update_transforms();
}

void Trs_tool::set_node_world_transform(const mat4 world_from_node)
{
    const auto target_node = m_target_node.lock();
    if (!target_node)
    {
        return;
    }
    const auto& target_parent = target_node->parent().lock();
    const mat4 parent_from_world = target_parent
        ? mat4{target_parent->node_from_world()} * world_from_node
        : world_from_node;
    target_node->set_parent_from_node(mat4{parent_from_world});
 }

void Trs_tool::update_for_view(Scene_view* scene_view)
{
    if (scene_view == nullptr)
    {
        return;
    }

    const auto camera = scene_view->get_camera();
    if (!camera)
    {
        return;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr)
    {
        return;
    }

    const vec3 view_position_in_world = vec3{camera_node->position_in_world()};

    erhe::scene::Scene* const view_scene = camera_node->get_scene();

    update_visibility(view_scene);
    m_visualization->update_scale(view_position_in_world);
    update_transforms();

    //if (root() == nullptr)
    //{
    //    return;
    //}
    //
    //const vec3  V0      = vec3{root()->position_in_world()} - vec3{camera->position_in_world()};
    //const vec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0});
    //const float v_dot_n = dot(V, m_rotation.normal);
    //->tail_log.trace("R: {} @ {}", root()->name(), root()->position_in_world());
    //->tail_log.trace("C: {} @ {}", camera->name(), camera->position_in_world());
    //->tail_log.trace("V: {}", vec3{V});
    //->tail_log.trace("N: {}", vec3{m_rotation.normal});
    //->tail_log.trace("V.N = {}", v_dot_n);
}

void Trs_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (context.camera != nullptr)
    {
        return;
    }
    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();

    if (m_cast_rays)
    {
        const auto target_node = m_target_node.lock();
        std::shared_ptr<erhe::scene::Mesh> mesh = as_mesh(target_node);
        if (mesh)
        {
            const auto* node = mesh->get_node();
            if (node != nullptr)
            {
                auto* scene_root = reinterpret_cast<Scene_root*>(node->node_data.host);
                if (scene_root != nullptr)
                    {
                        glm::vec3 directions[] = {
                            { 0.0f, -1.0f,  0.0f},
                            { 1.0f,  0.0f,  0.0f},
                            {-1.0f,  0.0f,  0.0f},
                            { 0.0f,  0.0f,  1.0f},
                            { 0.0f,  0.0f, -1.0f}
                        };
                        for (auto& d : directions)
                        {
                            auto& raytrace_scene = scene_root->raytrace_scene();
                            erhe::raytrace::Ray ray{
                                .origin    = node->position_in_world(),
                                .t_near    = 0.0f,
                                .direction = d,
                                .time      = 0.0f,
                                .t_far     = 9999.0f,
                                .mask      = Raytrace_node_mask::content,
                                .id        = 0,
                                .flags     = 0
                            };

                            erhe::raytrace::Hit hit;
                            if (project_ray(&raytrace_scene, mesh.get(), ray, hit))
                            {
                                Ray_hit_style ray_hit_style
                                {
                                    .ray_color     = glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
                                    .ray_thickness = 8.0f,
                                    .ray_length    = 0.5f,
                                    .hit_color     = glm::vec4{0.8f, 0.2f, 0.8f, 0.75f},
                                    .hit_thickness = 8.0f,
                                    .hit_size      = 0.10f
                                };

                                draw_ray_hit(line_renderer, ray, hit, ray_hit_style);
                            }
                        }
                    }
                }
            }
    }

    const auto target_node = m_target_node.lock();
    if (
        (erhe::application::g_line_renderer_set == nullptr) ||
        (get_handle_type(m_active_handle) != Handle_type::e_handle_type_rotate) ||
        (context.camera == nullptr) ||
        (!target_node)
    )
    {
        return;
    }

    const auto* camera_node = context.get_camera_node();
    if (camera_node == nullptr)
    {
        return;
    }

    const vec3  p                 = m_rotation.center_of_rotation;
    const vec3  n                 = m_rotation.normal;
    const vec3  side1             = m_rotation.reference_direction;
    const vec3  side2             = normalize(cross(n, side1));
    const vec3  position_in_world = target_node->position_in_world();
    const float distance          = length(position_in_world - vec3{camera_node->position_in_world()});
    const float scale             = m_visualization->get_scale() * distance / 100.0f;
    const float r1                = scale * 6.0f;

    constexpr vec4 red   {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr vec4 blue  {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr vec4 orange{1.0f, 0.5f, 0.0f, 0.8f};

    {
        const int sector_count = m_rotate_snap_enable
            ? static_cast<int>(glm::two_pi<float>() / glm::radians(m_rotate_snap))
            : 80;
        std::vector<vec3> positions;

        line_renderer.set_line_color(orange);
        line_renderer.set_thickness(-1.41f);
        for (int i = 0; i < sector_count + 1; ++i)
        {
            const float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
            const float theta = rel * glm::two_pi<float>();
            const bool  first = (i == 0);
            const bool  major = (i % 10 == 0);
            const float r0    =
                first
                    ? 0.0f
                    : major
                        ? 5.0f * scale
                        : 5.5f * scale;

            const vec3 p0 =
                p +
                r0 * std::cos(theta) * side1 +
                r0 * std::sin(theta) * side2;

            const vec3 p1 =
                p +
                r1 * std::cos(theta) * side1 +
                r1 * std::sin(theta) * side2;

            line_renderer.add_lines(
                {
                    {
                        p0,
                        p1
                    }
                }
            );
        }
    }

    // Circle (ring)
    {
        constexpr int sector_count = 200;
        std::vector<vec3> positions;
        for (int i = 0; i < sector_count + 1; ++i)
        {
            const float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
            const float theta = rel * glm::two_pi<float>();
            positions.emplace_back(
                p +
                r1 * std::cos(theta) * side1 +
                r1 * std::sin(theta) * side2
            );
        }
        for (size_t i = 0, count = positions.size(); i < count; ++i)
        {
            const std::size_t next_i = (i + 1) % count;
            line_renderer.add_lines(
                {
                    {
                        positions[i],
                        positions[next_i]
                    }
                }
            );
        }
    }

    const float snapped_angle = snap_rotate(m_rotation.current_angle);
    const auto snapped =
        p +
        r1 * std::cos(snapped_angle) * side1 +
        r1 * std::sin(snapped_angle) * side2;

    line_renderer.add_lines(red,                             { { p, r1 * side1 } } );
    line_renderer.add_lines(blue,                            { { p, snapped    } } );
    line_renderer.add_lines(get_axis_color(m_active_handle), { { p - 10.0f * n, p + 10.0f * n } } );
}

auto Trs_tool::get_active_handle() const -> Handle
{
    return m_active_handle;
}

auto Trs_tool::get_hover_handle() const -> Handle
{
    return m_hover_handle;
}

auto Trs_tool::get_handle(erhe::scene::Mesh* mesh) const -> Handle
{
    return m_visualization->get_handle(mesh);
}

auto Trs_tool::get_handle_type(const Handle handle) const -> Handle_type
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_translate_x:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_y:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_z:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_xy: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_xz: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_yz: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_rotate_x:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_y:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_z:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_none: return Handle_type::e_handle_type_none;
        default:
        {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

auto Trs_tool::get_axis_color(const Handle handle) const -> glm::vec4
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_translate_x: return glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        case Handle::e_handle_translate_y: return glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
        case Handle::e_handle_translate_z: return glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        case Handle::e_handle_rotate_x:    return glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_y:    return glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_z:    return glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        case Handle::e_handle_none:
        default:
        {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

void Trs_tool::update_transforms()
{
    const auto target_node = m_target_node.lock();
    if (!target_node)
    {
        return;
    }
    auto* scene_root = reinterpret_cast<Scene_root*>(target_node->node_data.host);
    if (scene_root == nullptr)
    {
        log_trs_tool->error("Node '{}' has no scene root", target_node->get_name());
        return;
    }
    m_visualization->update_transforms();
}

void Trs_tool::update_visibility(const erhe::scene::Scene* view_scene)
{
    const auto target_node = m_target_node.lock();
    const bool visible = target_node && (target_node->get_scene() == view_scene);
    m_visualization->update_visibility(visible);
    update_transforms();
}

auto Trs_tool::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return g_tools->get_tool_scene_root();
}

auto Trs_tool::get_target_scene_root() -> Scene_root*
{
    const auto node = m_target_node.lock();
    if (!node)
    {
        return {};
    }

    auto* scene_host = node->get_item_host();
    auto* scene_root = reinterpret_cast<Scene_root*>(scene_host);
    return scene_root;
}

} // namespace editor
