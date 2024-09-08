#include "tools/fly_camera_tool.hpp"
#include "tools/fly_camera_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <numeric>

namespace editor {

Jitter::Jitter() 
    : m_random_engine{m_random_device()}
    , m_distribution{0, 0}
{
}

void Jitter::imgui()
{
    ImGui::DragIntRange2("Jitter x 100ns", &m_min, &m_max, 1.0, 0, 1000000, "%d x 100ns", nullptr, ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemEdited()) {
        if (m_max < m_min) {
            std::swap(m_min, m_max);
        }
        m_distribution = std::uniform_int_distribution<int>{m_min, m_max};
    }
}

void Jitter::sleep()
{
    if (m_max == 0) {
        return;
    }
    std::uniform_int_distribution<int> distribution{m_min, m_max};
    int time_to_sleep = m_distribution(m_random_engine);
    if (time_to_sleep == 0) {
        return;
    }
    erhe::time::sleep_for_100ns(time_to_sleep);
}

auto Fly_camera_tool::try_ready() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_fly_camera->trace("Fly camera has no scene view");
        return false;
    }

    if (
        scene_view->get_hover(Hover_entry::tool_slot        ).valid ||
        scene_view->get_hover(Hover_entry::rendertarget_slot).valid
    ) {
        log_fly_camera->trace("Fly camera is hovering over tool or rendertarget");
        return false;
    }

    const Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view != nullptr) {
        // Exclude safe border near viewport edges from mouse interaction
        // to filter out viewport window resizing for example.
        const auto position_opt = viewport_scene_view->get_position_in_viewport();
        if (!position_opt.has_value()) {
            log_fly_camera->trace("Fly camera has no pointer position in viewport");
            return false;
        }
        constexpr float border   = 32.0f;
        const glm::vec2 position = position_opt.value();
        const erhe::math::Viewport viewport = viewport_scene_view->projection_viewport();
        if (
            (position.x <  border) ||
            (position.y <  border) ||
            (position.x >= viewport.width  - border) ||
            (position.y >= viewport.height - border)
        ) {
            log_fly_camera->trace("Fly camera has pointer position in border area that is ignored");
            return false;
        }
    }

    return true;
}

#pragma region Fly_camera_turn_command
Fly_camera_turn_command::Fly_camera_turn_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.turn_camera"}
    , m_context{context}
{
}

void Fly_camera_turn_command::try_ready()
{
    if (m_context.fly_camera_tool->try_ready()) {
        log_fly_camera->trace("Fly camera setting ready");
        set_ready();
    }
}

auto Fly_camera_turn_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    const auto value = input.variant.vector2.relative_value;
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            log_fly_camera->trace("Fly camera setting inactive due to nullptr hover scene view");
            set_inactive();
            return false;
        }
        if ((value.x != 0.0f) || (value.y != 0.0f)) {
            log_fly_camera->trace("Fly camera setting active due to pointer motion");
            set_active();
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        log_fly_camera->trace("Fly camera is not active");
        return false;
    }

    m_context.fly_camera_tool->turn_relative(input.timestamp, -value.x, -value.y);
    return true;
}
#pragma endregion Fly_camera_turn_command

#pragma region Fly_camera_tumble_command
Fly_camera_tumble_command::Fly_camera_tumble_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.tumble"}
    , m_context{context}
{
}

void Fly_camera_tumble_command::try_ready()
{
    if (m_context.fly_camera_tool->try_start_tumble()) {
        set_ready();
        m_context.fly_camera_tool->capture_pointer();
    }
}

auto Fly_camera_tumble_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    const auto value = input.variant.vector2.relative_value;
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            set_inactive();
            return false;
        }
        if ((value.x != 0.0f) || (value.y != 0.0f)) {
            set_active();
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.fly_camera_tool->tumble_relative(input.timestamp, -value.x, -value.y);
    return true;
}
void Fly_camera_tumble_command::on_inactive()
{
    m_context.fly_camera_tool->release_pointer();
}

#pragma endregion Fly_camera_tumble_command

#pragma region Fly_camera_track_command
Fly_camera_track_command::Fly_camera_track_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.track"}
    , m_context{context}
{
}

void Fly_camera_track_command::try_ready()
{
    if (m_context.fly_camera_tool->try_start_track()) {
        set_ready();
    }
}

auto Fly_camera_track_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            set_inactive();
            return false;
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.fly_camera_tool->track();
    return true;
}
#pragma endregion Fly_camera_turn_command

#pragma region Fly_camera_zoom_command
Fly_camera_zoom_command::Fly_camera_zoom_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.zoom_camera"}
    , m_context{context}
{
}

void Fly_camera_zoom_command::try_ready()
{
    if (m_context.fly_camera_tool->try_ready()) {
        set_ready();
    }
}

auto Fly_camera_zoom_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    set_ready();

    const auto value = input.variant.vector2.relative_value;
    //const auto state = get_command_state();
    //if (state == erhe::commands::State::Ready) {
    //    if (g_fly_camera_tool->get_hover_scene_view() == nullptr) {
    //        set_inactive();
    //        return false;
    //    }
    //    if (value.y != 0.0f) {
    //        set_active();
    //    }
    //}

    //if (get_command_state() != erhe::commands::State::Active) {
    //    return false;
    //}

    m_context.fly_camera_tool->zoom(input.timestamp, value.y);
    return true;
}
#pragma endregion Fly_camera_zoom_command

#pragma region Fly_camera_frame_command
Fly_camera_frame_command::Fly_camera_frame_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.frame"}
    , m_context{context}
{
}

auto Fly_camera_frame_command::try_call() -> bool
{
    Scene_view* scene_view = m_context.fly_camera_tool->get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    erhe::scene::Camera* camera = m_context.fly_camera_tool->get_camera();
    if (camera == nullptr) {
        return false;
    }
    erhe::scene::Node* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return false;
    }

    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    erhe::math::Viewport viewport = viewport_scene_view->projection_viewport();

    erhe::math::Bounding_box bbox{};
    const std::vector<std::shared_ptr<erhe::Item_base>>& selection = m_context.selection->get_selection();
    for (const std::shared_ptr<erhe::Item_base>& item : selection) {
        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
        if (!mesh) {
            continue;
        }
        const std::vector<erhe::primitive::Primitive>& primitives = mesh->get_primitives();
        for (const erhe::primitive::Primitive& primitive : primitives) {
            const auto bounding_box = primitive.get_bounding_box();
            if (!bounding_box.is_valid()) {
                continue;
            }
            erhe::math::Bounding_box world_bounding_box = bounding_box.transformed_by(node->world_from_node());
            bbox.include(world_bounding_box);
        }
    }
    if (!bbox.is_valid()) {
        return false;
    }

    glm::vec3 camera_position = camera_node->position_in_world();
    glm::vec3 target_position = bbox.center();
    glm::vec3 direction = target_position - camera_position;
    glm::vec3 direction_normalized = glm::normalize(target_position - camera_position);
    float size = glm::length(bbox.diagonal());
    erhe::scene::Projection::Fov_sides fov_sides = camera->projection()->get_fov_sides(viewport);
    float min_fov_side = std::numeric_limits<float>::max();
    for (float fov_side : { fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down }) {
        min_fov_side = std::min(std::abs(fov_side), min_fov_side);
    }
    float tan_fov_side = std::tanf(min_fov_side);
    float fit_distance = size / (2.0f * tan_fov_side);
    glm::vec3 new_position = target_position - fit_distance * direction_normalized;
    glm::mat4 new_world_from_node = erhe::math::create_look_at(new_position, target_position, glm::vec3{0.0f, 1.0f, 0.0});
    camera_node->set_world_from_node(new_world_from_node);
    return true;
}
#pragma endregion Fly_camera_frame_command

#pragma region Fly_camera_move_command
Fly_camera_move_command::Fly_camera_move_command(
    erhe::commands::Commands&            commands,
    Editor_context&                      context,
    const Variable                       variable,
    const erhe::math::Input_axis_control control,
    const bool                           active
)
    : Command   {commands, "Fly_camera.move"}
    , m_context {context}
    , m_variable{variable}
    , m_control {control }
    , m_active  {active  }
{
}

auto Fly_camera_move_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_context.fly_camera_tool->try_move(input.timestamp, m_variable, m_control, m_active);
}
#pragma endregion Fly_camera_move_command

#pragma region Fly_camera_variable_float_command
Fly_camera_variable_float_command::Fly_camera_variable_float_command(
    erhe::commands::Commands& commands,
    Editor_context&           context,
    Variable                  variable,
    float                     scale
)
    : Command   {commands, ""}
    , m_context {context}
    , m_variable{variable}
    , m_scale   {scale}
{
}

auto Fly_camera_variable_float_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_context.fly_camera_tool->adjust(input.timestamp, m_variable, input.variant.float_value * m_scale);
}
#pragma endregion Fly_camera_variable_float_command

Fly_camera_tool::Fly_camera_tool(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Time&                        time,
    Tools&                       tools
)
    : Update_time_base                {time}
    , erhe::imgui::Imgui_window       {imgui_renderer, imgui_windows, "Fly Camera", "fly_camera"}
    , Tool                            {editor_context}
    , m_turn_command                  {commands, editor_context}
    , m_tumble_command                {commands, editor_context}
    , m_track_command                 {commands, editor_context}
    , m_zoom_command                  {commands, editor_context}
    , m_frame_command                 {commands, editor_context}
    , m_move_up_active_command        {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::more, true }
    , m_move_up_inactive_command      {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::more, false}
    , m_move_down_active_command      {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::less, true }
    , m_move_down_inactive_command    {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::less, false}
    , m_move_left_active_command      {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::less, true }
    , m_move_left_inactive_command    {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::less, false}
    , m_move_right_active_command     {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::more, true }
    , m_move_right_inactive_command   {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::more, false}
    , m_move_forward_active_command   {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::less, true }
    , m_move_forward_inactive_command {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::less, false}
    , m_move_backward_active_command  {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::more, true }
    , m_move_backward_inactive_command{commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::more, false}
    , m_translate_x_command           {commands, editor_context, Variable::translate_x,  128.0f}
    , m_translate_y_command           {commands, editor_context, Variable::translate_y, -128.0f}
    , m_translate_z_command           {commands, editor_context, Variable::translate_z,  128.0f}
    , m_rotate_x_command              {commands, editor_context, Variable::rotate_x,     0.6f}
    , m_rotate_y_command              {commands, editor_context, Variable::rotate_y,    -0.6f}
    , m_rotate_z_command              {commands, editor_context, Variable::rotate_z,     0.6f}

    , m_velocity_graph                {"Velocity",           "time", "ms", "velocity",           "m/s"}
    , m_distance_graph                {"Distance",           "time", "ms", "distance",           "m"}
    , m_distance_dt_graph             {"Distance / dt",      "time", "ms", "distance / dt",      "m/s"}
    , m_state_time_graph              {"State time",         "time", "ms", "state time",         "ms"}
    , m_deltatime_graph               {"Delta-Time",         "time", "ms", "dt",                 "ms"}
    , m_reference_velocity_graph      {"Reference Velocity", "time", "ms", "reference velocity", "m/s"}
{
    m_velocity_graph   .path_color  = 0xffcccccc;
    m_distance_graph   .path_color  = 0xffcc88cc;
    m_distance_dt_graph.path_color  = 0xff88cccc;
    m_state_time_graph .path_color  = 0xffcccc88;
    m_deltatime_graph  .path_color  = 0xff22cc88;
    m_reference_velocity_graph.path_color = 0x886666ff;
    m_reference_velocity_graph.draw_keys = false;

    m_velocity_graph   .key_color   = 0xffaaaaaa;
    m_distance_graph   .key_color   = 0xffaa66aa;
    m_distance_dt_graph.key_color   = 0xff66aaaa;
    m_state_time_graph .key_color   = 0xffaaaa66;
    m_deltatime_graph  .key_color   = 0xff33aa66;
    m_reference_velocity_graph.key_color = 0x00000000;

    m_velocity_graph   .hover_color = 0xffffffff;
    m_distance_graph   .hover_color = 0xffffbbff;
    m_distance_dt_graph.hover_color = 0xffbbffff;
    m_state_time_graph .hover_color = 0xffffffbb;
    m_deltatime_graph  .hover_color = 0xffbbffbb;
    m_reference_velocity_graph.hover_color = 0x88555555;

    if (editor_context.OpenXR) {
        return;
    }

    m_camera_controller = std::make_shared<Frame_controller>();

    m_camera_controller->get_variable(Variable::translate_x).set_power_base(config.move_power);
    m_camera_controller->get_variable(Variable::translate_y).set_power_base(config.move_power);
    m_camera_controller->get_variable(Variable::translate_z).set_power_base(config.move_power);
    m_camera_controller->get_variable(Variable::rotate_x   ).set_power_base(65536.0f * 65536.0f);
    m_camera_controller->get_variable(Variable::rotate_y   ).set_power_base(65536.0f * 65536.0f);
    m_camera_controller->get_variable(Variable::rotate_z   ).set_power_base(65536.0f * 65536.0f);

    auto ini = erhe::configuration::get_ini("erhe.ini", "camera_controls");
    ini->get("invert_x",   config.invert_x);
    ini->get("invert_y",   config.invert_y);
    ini->get("move_power", config.move_power);
    ini->get("move_speed", m_camera_controller->move_speed);
    ini->get("turn_speed", config.turn_speed);

    set_base_priority(c_priority);
    set_description  ("Fly Camera");
    set_flags        (Tool_flags::background);

    tools.register_tool(this);

    commands.register_command(&m_move_up_active_command);
    commands.register_command(&m_move_up_inactive_command);
    commands.register_command(&m_move_down_active_command);
    commands.register_command(&m_move_down_inactive_command);
    commands.register_command(&m_move_left_active_command);
    commands.register_command(&m_move_left_inactive_command);
    commands.register_command(&m_move_right_active_command);
    commands.register_command(&m_move_right_inactive_command);
    commands.register_command(&m_move_forward_active_command);
    commands.register_command(&m_move_forward_inactive_command);
    commands.register_command(&m_move_backward_active_command);
    commands.register_command(&m_move_backward_inactive_command);
    commands.register_command(&m_translate_x_command);
    commands.register_command(&m_translate_y_command);
    commands.register_command(&m_translate_z_command);
    commands.register_command(&m_rotate_x_command);
    commands.register_command(&m_rotate_y_command);
    commands.register_command(&m_rotate_z_command);

    commands.bind_command_to_key(&m_move_up_active_command,         erhe::window::Key_q, true );
    commands.bind_command_to_key(&m_move_up_inactive_command,       erhe::window::Key_q, false);
    commands.bind_command_to_key(&m_move_down_active_command,       erhe::window::Key_e, true );
    commands.bind_command_to_key(&m_move_down_inactive_command,     erhe::window::Key_e, false);
    commands.bind_command_to_key(&m_move_left_active_command,       erhe::window::Key_a, true );
    commands.bind_command_to_key(&m_move_left_inactive_command,     erhe::window::Key_a, false);
    commands.bind_command_to_key(&m_move_right_active_command,      erhe::window::Key_d, true );
    commands.bind_command_to_key(&m_move_right_inactive_command,    erhe::window::Key_d, false);
    commands.bind_command_to_key(&m_move_forward_active_command,    erhe::window::Key_w, true );
    commands.bind_command_to_key(&m_move_forward_inactive_command,  erhe::window::Key_w, false);
    commands.bind_command_to_key(&m_move_backward_active_command,   erhe::window::Key_s, true );
    commands.bind_command_to_key(&m_move_backward_inactive_command, erhe::window::Key_s, false);

    commands.register_command(&m_turn_command);
    commands.bind_command_to_mouse_drag(&m_turn_command, erhe::window::Mouse_button_left, false, 0);

    commands.register_command(&m_tumble_command);
    commands.bind_command_to_mouse_drag(&m_tumble_command, erhe::window::Mouse_button_left, true, erhe::window::Key_modifier_bit_menu);

    commands.register_command(&m_track_command);
    commands.bind_command_to_mouse_drag(&m_track_command, erhe::window::Mouse_button_middle, true, erhe::window::Key_modifier_bit_menu);

    commands.register_command(&m_zoom_command);
    commands.bind_command_to_mouse_wheel(&m_zoom_command);

    commands.register_command(&m_frame_command);
    commands.bind_command_to_key(&m_frame_command, erhe::window::Key_f, true);

    m_rotate_scale_x = config.invert_x ? -1.0f / 1024.0f : 1.0f / 1024.f;
    m_rotate_scale_y = config.invert_y ? -1.0f / 1024.0f : 1.0f / 1024.f;

    commands.bind_command_to_controller_axis(&m_translate_x_command, 0);
    commands.bind_command_to_controller_axis(&m_translate_y_command, 2);
    commands.bind_command_to_controller_axis(&m_translate_z_command, 1);
    commands.bind_command_to_controller_axis(&m_rotate_x_command, 3);
    commands.bind_command_to_controller_axis(&m_rotate_y_command, 5);
    commands.bind_command_to_controller_axis(&m_rotate_z_command, 4);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            Tool::on_message(message);
            using namespace erhe::bit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                on_hover_viewport_change();
            }
        }
    );

    m_turn_command                  .set_host(this);
    m_tumble_command                .set_host(this);
    m_zoom_command                  .set_host(this);
    m_frame_command                 .set_host(this);
    m_move_up_active_command        .set_host(this);
    m_move_up_inactive_command      .set_host(this);
    m_move_down_active_command      .set_host(this);
    m_move_down_inactive_command    .set_host(this);
    m_move_left_active_command      .set_host(this);
    m_move_left_inactive_command    .set_host(this);
    m_move_right_active_command     .set_host(this);
    m_move_right_inactive_command   .set_host(this);
    m_move_forward_active_command   .set_host(this);
    m_move_forward_inactive_command .set_host(this);
    m_move_backward_active_command  .set_host(this);
    m_move_backward_inactive_command.set_host(this);
}

void Fly_camera_tool::update_camera()
{
    if (m_context.OpenXR) {
        return;
    }
    if (!m_use_viewport_camera) {
        return;
    }

    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    const auto camera = (viewport_scene_view)
        ? viewport_scene_view->get_camera()
        : std::shared_ptr<erhe::scene::Camera>{};
    const auto* camera_node = camera ? camera->get_node() : nullptr;

    // TODO This is messy

    if (m_camera_controller->get_node() != camera_node) {
        set_camera(camera.get());
    }
}

void Fly_camera_tool::set_camera(erhe::scene::Camera* const camera, erhe::scene::Node* node)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.
    if ((node == nullptr) && (camera != nullptr)) {
        node = camera->get_node();
    }

    if (node != nullptr) {
        auto* scene_root = static_cast<Scene_root*>(node->node_data.host);
        if (scene_root != nullptr) {
            scene_root->get_scene().update_node_transforms();
        } else {
            log_fly_camera->warn("node does not have scene root");
        }
    }

    m_camera_controller->set_node(node);
    m_camera = camera;
    m_node = node;
}

auto Fly_camera_tool::get_camera() const -> erhe::scene::Camera*
{
    return m_camera;
}

void Fly_camera_tool::translation(std::chrono::steady_clock::time_point timestamp, const int tx, const int ty, const int tz)
{
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    float x = static_cast<float>(tx) / 256.0f;
    float y = static_cast<float>(ty) / 256.0f;
    float z = static_cast<float>(tz) / 256.0f;

    m_camera_controller->translate_x.adjust(timestamp, x);
    m_camera_controller->translate_y.adjust(timestamp, y);
    m_camera_controller->translate_z.adjust(timestamp, z);
}

void Fly_camera_tool::rotation(std::chrono::steady_clock::time_point timestamp, const int rx, const int ry, const int rz)
{
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->rotate_x.adjust(timestamp, config.turn_speed * static_cast<float>(rx) / scale);
    m_camera_controller->rotate_y.adjust(timestamp, config.turn_speed * static_cast<float>(ry) / scale);
    m_camera_controller->rotate_z.adjust(timestamp, config.turn_speed * static_cast<float>(rz) / scale);
}

void Fly_camera_tool::on_hover_viewport_change()
{
    m_camera_controller->translate_x.reset();
    m_camera_controller->translate_y.reset();
    m_camera_controller->translate_z.reset();
}

auto Fly_camera_tool::adjust(std::chrono::steady_clock::time_point timestamp, Variable variable, float value) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if ((get_hover_scene_view() == nullptr)) {
        m_camera_controller->translate_x.reset();
        m_camera_controller->translate_y.reset();
        m_camera_controller->translate_z.reset();
        m_camera_controller->rotate_x.reset();
        m_camera_controller->rotate_y.reset();
        m_camera_controller->rotate_z.reset();
        return false;
    }

    auto& controller = m_camera_controller->get_variable(variable);
    controller.adjust(timestamp, value);
    return true;
}

auto Fly_camera_tool::try_move(std::chrono::steady_clock::time_point timestamp, const Variable variable, const erhe::math::Input_axis_control control, const bool active) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if ((get_hover_scene_view() == nullptr) && active) {
        m_camera_controller->translate_x.reset();
        m_camera_controller->translate_y.reset();
        m_camera_controller->translate_z.reset();
        return false;
    }

    log_fly_camera->trace("begin try_move");
    auto& controller = m_camera_controller->get_variable(variable);
    controller.set(timestamp, control, active);
    if (m_recording) {
        record_sample(timestamp);
        std::chrono::duration<float> time = timestamp - m_recording_start_time;
        float t = 1000.0f * time.count();
        std::string message = fmt::format("{} @ {}ms", active ? "press" : "release", t);
        m_events.emplace_back(t, message);
        m_log_frame_update_details = true;
    }
    log_fly_camera->trace("end try_move");
    return true;
}

auto Fly_camera_tool::zoom(std::chrono::steady_clock::time_point timestamp, const float delta) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (delta != 0.0f) {
        glm::vec3 position = m_camera_controller->get_position();
        const float l = glm::length(position);
        const float k = (-1.0f / 32.0f) * l * delta;
        m_camera_controller->get_variable(Variable::translate_z).adjust(timestamp, k);
    }

    return true;
}

auto Fly_camera_tool::turn_relative(std::chrono::steady_clock::time_point timestamp, const float dx, const float dy) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    const float rx = config.turn_speed * dy * m_rotate_scale_y;
    const float ry = config.turn_speed * dx * m_rotate_scale_x;
    if (false) {
        m_camera_controller->apply_rotation(rx, ry, 0.0f);
    } else {
        m_camera_controller->get_variable(Variable::rotate_x).adjust(timestamp, rx / 2.0f);
        m_camera_controller->get_variable(Variable::rotate_y).adjust(timestamp, ry / 2.0f);
    }

    return true;
}

void Fly_camera_tool::capture_pointer()
{
    if (!m_context.OpenXR) {
        m_context.context_window->capture_mouse(true);
    }
}

void Fly_camera_tool::release_pointer()
{
    if (!m_context.OpenXR) {
        m_context.context_window->capture_mouse(false);
    }
}

auto Fly_camera_tool::try_start_tumble() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if (hover == nullptr) {
        return false;
    }

    if (!hover->position.has_value()) {
        return false;
    }

    m_tumble_pivot = hover->position;
    return true;
}

auto Fly_camera_tool::tumble_relative(std::chrono::steady_clock::time_point timestamp, float dx, float dy) -> bool
{
    static_cast<void>(timestamp); // TODO consider how to correctly the time into account here
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    if (!viewport_scene_view) {
        return false;
    }

    if (!m_tumble_pivot.has_value()) {
        return false;
    }

    const float rx = config.turn_speed * dy * m_rotate_scale_y;
    const float ry = config.turn_speed * dx * m_rotate_scale_x;
    m_camera_controller->apply_tumble(m_tumble_pivot.value(), rx, ry, 0.0f);
    return true;
}

auto Fly_camera_tool::try_start_track() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if (hover == nullptr) {
        return false;
    }

    if (!hover->position.has_value()) {
        return false;
    }

    m_track_plane_point = hover->position;
    m_track_plane_normal = m_camera_controller->get_axis_z();
    return true;
}

auto Fly_camera_tool::track() -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }

    if (!m_track_plane_point.has_value() || !m_track_plane_normal.has_value()) {
        return false;
    }

    auto get_track_position = [&]() -> std::optional<glm::vec3>
    {
        const std::optional<glm::vec3> ray_origin_opt    = scene_view->get_control_ray_origin_in_world();
        const std::optional<glm::vec3> ray_direction_opt = scene_view->get_control_ray_direction_in_world();
        if (!ray_origin_opt.has_value() || !ray_direction_opt.has_value()) {
            return {};
        }
        const glm::vec3 ray_origin    = ray_origin_opt   .value();
        const glm::vec3 ray_direction = ray_direction_opt.value();
        std::optional<float> t = erhe::math::intersect_plane<float>(m_track_plane_normal.value(), m_track_plane_point.value(), ray_origin, ray_direction);
        if (!t.has_value()) {
            return {};
        }
        return ray_origin + t.value() * ray_direction;
    };

    std::optional<glm::vec3> hover_position = get_track_position();
    if (!hover_position.has_value()) {
        return false;
    }

    glm::vec3 translation = hover_position.value() - m_track_plane_point.value();
    if (glm::length(translation) < 0.001f) {
        return true;
    }

    glm::vec3 old_position = m_camera_controller->get_position();
    glm::vec3 new_position = old_position - translation;
    m_camera_controller->set_position(new_position);
    m_camera_controller->get_node()->update_world_from_node();
    viewport_scene_view->update_hover(true);

    m_track_plane_point = get_track_position();
    return true;
}

//void Fly_camera_tool::update_fixed_step(const Time_context&)
//{
//    if (!m_camera_controller) { // TODO
//        return; 
//    }
//
//    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};
//
//    m_camera_controller->update_fixed_step();
//}

void Fly_camera_tool::record_sample(std::chrono::steady_clock::time_point)
{
    if (!m_recording) {
        return;
    }
    erhe::math::Input_axis& input_axis = m_camera_controller->translate_x;
    erhe::scene::Node* node = m_camera_controller->get_node();
    if (node == nullptr) {
        return;
    }
    auto  timestamp_0  = input_axis.get_segment_timestamp(0);
    auto  timestamp_1  = input_axis.get_segment_timestamp(1);
    std::chrono::duration<float> time_0 = timestamp_0 - m_recording_start_time;
    std::chrono::duration<float> time_1 = timestamp_1 - m_recording_start_time;
    float t_0          = time_0.count();
    float t_1          = time_1.count();
    float dt           = t_1 - t_0;
    float velocity_0   = input_axis.get_segment_velocity(0);
    float velocity_1   = input_axis.get_segment_velocity(1);
    float distance_0   = input_axis.get_segment_distance(0);
    float distance_1   = input_axis.get_segment_distance(1);
    float state_time_0 = input_axis.get_segment_state_time(0);
    float state_time_1 = input_axis.get_segment_state_time(1);
    float distance     = distance_1 - distance_0;
    m_velocity_graph   .samples.push_back({ImVec2{1000.0f * t_0, velocity_0},             ImVec2{1000.0f * t_1, velocity_1}});
    m_distance_graph   .samples.push_back({ImVec2{1000.0f * t_0, distance_0},             ImVec2{1000.0f * t_1, distance_1}});
    m_distance_dt_graph.samples.push_back({ImVec2{1000.0f * t_0, distance / dt},          ImVec2{1000.0f * t_1, distance / dt}});
    m_state_time_graph .samples.push_back({ImVec2{1000.0f * t_0, 1000.0f * state_time_0}, ImVec2{1000.0f * t_1, 1000.0f * state_time_1}});
    m_deltatime_graph  .samples.push_back({ImVec2{1000.0f * t_0, 1000.0f * dt},           ImVec2{1000.0f * t_1, 1000.0f * dt}});
    float state_time_diff         = state_time_1 - state_time_0;
    float last_t                  = t_0;
    float last_reference_velocity = input_axis.evaluate_velocity_at_state_time(state_time_0);
    float velocity_min = std::min(velocity_0, velocity_1);
    float velocity_max = std::max(velocity_0, velocity_1);
    for (int i = 1; i <= 10; ++i) {
        float rel                = static_cast<float>(i) / 10.0f;
        float t                  = t_0          + rel * dt;
        float state_time         = state_time_0 + rel * state_time_diff;
        float reference_velocity = input_axis.evaluate_velocity_at_state_time(state_time);
        m_reference_velocity_graph.samples.push_back(
            {
                ImVec2{1000.0f * last_t, last_reference_velocity},
                ImVec2{1000.0f * t,      reference_velocity}
            }
        );
        last_t                  = t;
        last_reference_velocity = reference_velocity;
        if (reference_velocity < velocity_min) {
            float diff = std::abs(reference_velocity - velocity_min);
            if (diff > 0.1f) {
                log_fly_camera->warn("reference velocity sanity check failed @ {}", 1000.0f * t);
            }
        }
        if (reference_velocity > velocity_max) {
            float diff = std::abs(reference_velocity - velocity_max);
            if (diff > 0.1f) {
                log_fly_camera->warn("reference velocity sanity check failed @ {}", 1000.0f * t);
            }
        }
    }
}

void Fly_camera_tool::on_frame_begin()
{
    m_camera_controller->on_frame_begin();
}

void Fly_camera_tool::on_frame_end()
{
    m_camera_controller->on_frame_end();

    m_jitter.sleep();
}

void Fly_camera_tool::update_once_per_frame(const Time_context& time_context)
{
    if (!m_camera_controller) { // TODO
        return; 
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    update_camera();
    m_camera_controller->tick(time_context.timestamp);

    record_sample(time_context.timestamp);
}

auto simple_degrees(const float radians_value) -> float
{
    const auto degrees_value   = glm::degrees(radians_value);
    const auto degrees_mod_360 = std::fmod(degrees_value, 360.0f);
    return (degrees_mod_360 <= 180.0f)
        ? degrees_mod_360
        : degrees_mod_360 - 360.0f;
}

void Fly_camera_tool::show_input_axis_ui(erhe::math::Input_axis& input_axis) const
{
    ImGui::PushID(input_axis.get_name().data());

    bool  less       = input_axis.get_less();
    bool  more       = input_axis.get_more();
    float power_base = input_axis.get_power_base();
    float velocity   = input_axis.get_velocity();
    float distance   = input_axis.get_tick_distance();
    float v0         = input_axis.get_base_velocity();

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(input_axis.get_name().data());
    if (less) { ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("<"); }
    if (more) { ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(">"); }

    ImGui::TableSetColumnIndex(3); 
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(3);
    ImGui::SliderFloat("##", &power_base, 1.0f, 100000.0, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) {
        input_axis.set_power_base(power_base);
    }
    ImGui::PopID();

    ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Text("%.7f", velocity);
    ImGui::TableSetColumnIndex(5); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Text("%.3f", distance);
    ImGui::TableSetColumnIndex(6); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Text("%.3f", v0);

    ImGui::PopID();
}

void Fly_camera_tool::imgui()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    ImGui::Checkbox   ("Use Viewport Camera", &m_use_viewport_camera);
    ImGui::SliderFloat("Move Power Base", &config.move_power, 1.0f, 10000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemEdited()) {
        m_camera_controller->translate_x.set_power_base(config.move_power);
        m_camera_controller->translate_y.set_power_base(config.move_power);
        m_camera_controller->translate_z.set_power_base(config.move_power);
    }
    ImGui::SliderFloat("Move Speed", &m_camera_controller->move_speed, 0.01f, 200.0f);
    ImGui::SliderFloat("Turn Speed", &config.turn_speed, 0.2f, 2.0f);

    //erhe::math::Input_axis& control = m_camera_controller->translate_x;
    if (ImGui::TreeNodeEx("Controls", ImGuiTreeNodeFlags_None)) {
        ImGui::BeginTable("Controls", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 5.0f);
        ImGui::TableSetupColumn("Less", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("More", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("exp",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("vel",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("dst",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("v0",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        show_input_axis_ui(m_camera_controller->translate_x);
        show_input_axis_ui(m_camera_controller->translate_y);
        show_input_axis_ui(m_camera_controller->translate_z);
        show_input_axis_ui(m_camera_controller->rotate_x);
        show_input_axis_ui(m_camera_controller->rotate_y);
        show_input_axis_ui(m_camera_controller->rotate_z);
        show_input_axis_ui(m_camera_controller->speed_modifier);
        ImGui::EndTable();
        ImGui::TreePop();
    }

    if (!m_recording) {
        if (ImGui::Button("Start Recording")) {
            m_events           .clear();

            m_velocity_graph          .clear();
            m_distance_graph          .clear();
            m_distance_dt_graph       .clear();
            m_state_time_graph        .clear();
            m_deltatime_graph         .clear();
            m_reference_velocity_graph.clear();
            m_recording_start_time = std::chrono::steady_clock::now();
            m_recording = true;
        }
    } else {
        if (ImGui::Button("Stop Recording")) {
            m_recording = false;
        }
    }

    m_jitter.imgui();

                       ImGui::Checkbox("Velocity",           &m_velocity_graph          .plot);
    ImGui::SameLine(); ImGui::Checkbox("Reference Velocity", &m_reference_velocity_graph.plot);
    ImGui::SameLine(); ImGui::Checkbox("Distance",           &m_distance_graph          .plot);
    ImGui::SameLine(); ImGui::Checkbox("Distance / dt",      &m_distance_dt_graph       .plot);
    ImGui::SameLine(); ImGui::Checkbox("State Time",         &m_state_time_graph        .plot);
    ImGui::SameLine(); ImGui::Checkbox("Delta-Time",         &m_deltatime_graph         .plot);

    if (m_velocity_graph   .plot) ImGui::SliderFloat("Velocity Scale",      &m_velocity_graph   .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    if (m_distance_graph   .plot) ImGui::SliderFloat("Distance Scale",      &m_distance_graph   .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    if (m_distance_dt_graph.plot) ImGui::SliderFloat("Distance / dt Scale", &m_distance_dt_graph.y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    if (m_state_time_graph .plot) ImGui::SliderFloat("State Time Scale",    &m_state_time_graph .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    if (m_deltatime_graph  .plot) ImGui::SliderFloat("Delta Time Scale",    &m_deltatime_graph  .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);

    m_reference_velocity_graph.y_scale = m_velocity_graph.y_scale;

    if (m_graph_plotter.begin()) {
        for (const Event& event : m_events) {
            m_graph_plotter.sample_x_line(event.x, 0x88008800);
        }
        m_graph_plotter.plot(m_velocity_graph          );
        m_graph_plotter.plot(m_reference_velocity_graph);
        m_graph_plotter.plot(m_distance_graph          );
        m_graph_plotter.plot(m_distance_dt_graph       );
        m_graph_plotter.plot(m_state_time_graph        );
        m_graph_plotter.plot(m_deltatime_graph         );
        for (const Event& event : m_events) {
            m_graph_plotter.sample_text(event.x, 0.0f, event.text.c_str(), 0xff00ff00);
        }
        m_graph_plotter.end();
    }
#endif
}

}
