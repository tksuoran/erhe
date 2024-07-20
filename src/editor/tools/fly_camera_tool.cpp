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
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <numeric>

namespace editor {

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

    m_context.fly_camera_tool->turn_relative(-value.x, -value.y);
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

    m_context.fly_camera_tool->tumble_relative(-value.x, -value.y);
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

    m_context.fly_camera_tool->zoom(value.y);
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
    erhe::commands::Commands&                     commands,
    Editor_context&                               context,
    const Variable                                variable,
    const erhe::math::Simulation_variable_control control,
    const bool                                    active
)
    : Command   {commands, "Fly_camera.move"}
    , m_context {context}
    , m_variable{variable}
    , m_control {control }
    , m_active  {active  }
{
}

auto Fly_camera_move_command::try_call() -> bool
{
    return m_context.fly_camera_tool->try_move(m_variable, m_control, m_active);
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
    return m_context.fly_camera_tool->adjust(m_variable, input.variant.float_value * m_scale);
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
    , m_move_up_active_command        {commands, editor_context, Variable::translate_y, erhe::math::Simulation_variable_control::more, true }
    , m_move_up_inactive_command      {commands, editor_context, Variable::translate_y, erhe::math::Simulation_variable_control::more, false}
    , m_move_down_active_command      {commands, editor_context, Variable::translate_y, erhe::math::Simulation_variable_control::less, true }
    , m_move_down_inactive_command    {commands, editor_context, Variable::translate_y, erhe::math::Simulation_variable_control::less, false}
    , m_move_left_active_command      {commands, editor_context, Variable::translate_x, erhe::math::Simulation_variable_control::less, true }
    , m_move_left_inactive_command    {commands, editor_context, Variable::translate_x, erhe::math::Simulation_variable_control::less, false}
    , m_move_right_active_command     {commands, editor_context, Variable::translate_x, erhe::math::Simulation_variable_control::more, true }
    , m_move_right_inactive_command   {commands, editor_context, Variable::translate_x, erhe::math::Simulation_variable_control::more, false}
    , m_move_forward_active_command   {commands, editor_context, Variable::translate_z, erhe::math::Simulation_variable_control::less, true }
    , m_move_forward_inactive_command {commands, editor_context, Variable::translate_z, erhe::math::Simulation_variable_control::less, false}
    , m_move_backward_active_command  {commands, editor_context, Variable::translate_z, erhe::math::Simulation_variable_control::more, true }
    , m_move_backward_inactive_command{commands, editor_context, Variable::translate_z, erhe::math::Simulation_variable_control::more, false}
    , m_translate_x_command           {commands, editor_context, Variable::translate_x,  0.3f}
    , m_translate_y_command           {commands, editor_context, Variable::translate_y, -0.3f}
    , m_translate_z_command           {commands, editor_context, Variable::translate_z,  0.3f}
    , m_rotate_x_command              {commands, editor_context, Variable::rotate_x,     0.6f}
    , m_rotate_y_command              {commands, editor_context, Variable::rotate_y,    -0.6f}
    , m_rotate_z_command              {commands, editor_context, Variable::rotate_z,     0.6f}
{
    if (editor_context.OpenXR) {
        return;
    }

    auto ini = erhe::configuration::get_ini("erhe.ini", "camera_controls");
    ini->get("invert_x",           config.invert_x);
    ini->get("invert_y",           config.invert_y);
    ini->get("velocity_damp",      config.velocity_damp);
    ini->get("velocity_max_delta", config.velocity_max_delta);
    ini->get("sensitivity",        config.sensitivity);

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

    m_camera_controller = std::make_shared<Frame_controller>();

    m_camera_controller->get_variable(Variable::translate_x).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_y).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_z).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);

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

void Fly_camera_tool::translation(const int tx, const int ty, const int tz)
{
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    float x = static_cast<float>(tx) * m_camera_controller->translate_x.max_delta() / 256.0f;
    float y = static_cast<float>(ty) * m_camera_controller->translate_y.max_delta() / 256.0f;
    float z = static_cast<float>(tz) * m_camera_controller->translate_z.max_delta() / 256.0f;

    m_camera_controller->translate_x.adjust(x);
    m_camera_controller->translate_y.adjust(y);
    m_camera_controller->translate_z.adjust(z);
}

void Fly_camera_tool::rotation(const int rx, const int ry, const int rz)
{
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->rotate_x.adjust(m_sensitivity * static_cast<float>(rx) / scale);
    m_camera_controller->rotate_y.adjust(m_sensitivity * static_cast<float>(ry) / scale);
    m_camera_controller->rotate_z.adjust(m_sensitivity * static_cast<float>(rz) / scale);
}

void Fly_camera_tool::on_hover_viewport_change()
{
    m_camera_controller->translate_x.reset();
    m_camera_controller->translate_y.reset();
    m_camera_controller->translate_z.reset();
}

auto Fly_camera_tool::adjust(Variable variable, float value) -> bool
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
    controller.adjust(value);
    return true;
}

auto Fly_camera_tool::try_move(const Variable variable, const erhe::math::Simulation_variable_control control, const bool active) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if ((get_hover_scene_view() == nullptr) && active) {
        m_camera_controller->translate_x.reset();
        m_camera_controller->translate_y.reset();
        m_camera_controller->translate_z.reset();
        return false;
    }

    auto& controller = m_camera_controller->get_variable(variable);
    controller.set(control, active);

    return true;
}

auto Fly_camera_tool::zoom(const float delta) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (delta != 0.0f) {
        glm::vec3 position = m_camera_controller->get_position();
        const float l = glm::length(position);
        const float k = (-1.0f / 32.0f) * l * delta;
        m_camera_controller->get_variable(Variable::translate_z).adjust(k);
    }

    return true;
}

auto Fly_camera_tool::turn_relative(const float dx, const float dy) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    const float rx = m_sensitivity * dy * m_rotate_scale_y;
    const float ry = m_sensitivity * dx * m_rotate_scale_x;
    if (false) {
        m_camera_controller->apply_rotation(rx, ry, 0.0f);
    } else {
        m_camera_controller->get_variable(Variable::rotate_x).adjust(rx / 2.0f);
        m_camera_controller->get_variable(Variable::rotate_y).adjust(ry / 2.0f);
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

auto Fly_camera_tool::tumble_relative(float dx, float dy) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    if (!viewport_scene_view) {
        return false;
    }

    if (!m_tumble_pivot.has_value()) {
        return false;
    }

    const float rx = m_sensitivity * dy * m_rotate_scale_y;
    const float ry = m_sensitivity * dx * m_rotate_scale_x;
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

void Fly_camera_tool::update_fixed_step(const Time_context&)
{
    if (!m_camera_controller) { // TODO
        return; 
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    m_camera_controller->update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(const Time_context&)
{
    if (!m_camera_controller) { // TODO
        return; 
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    update_camera();
    m_camera_controller->update();
}

auto simple_degrees(const float radians_value) -> float
{
    const auto degrees_value   = glm::degrees(radians_value);
    const auto degrees_mod_360 = std::fmod(degrees_value, 360.0f);
    return (degrees_mod_360 <= 180.0f)
        ? degrees_mod_360
        : degrees_mod_360 - 360.0f;
}

void Fly_camera_tool::imgui()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    ImGui::Checkbox   ("Use Viewport Camera", &m_use_viewport_camera);
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);

    if (m_camera_controller) {
        float speed = m_camera_controller->translate_z.max_delta();
        ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);

        m_camera_controller->translate_x.set_max_delta(speed);
        m_camera_controller->translate_y.set_max_delta(speed);
        m_camera_controller->translate_z.set_max_delta(speed);
    }
#endif
}

}
