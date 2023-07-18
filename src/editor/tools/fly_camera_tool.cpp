#include "tools/fly_camera_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"

#include "erhe/commands/input_arguments.hpp"
#include "erhe/commands/commands.hpp"
#include "erhe/configuration/configuration.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window_event_handler.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

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

    const Viewport_window* viewport_window = scene_view->as_viewport_window();
    if (viewport_window != nullptr) {
        // Exclude safe border near viewport edges from mouse interaction
        // to filter out viewport window resizing for example.
        const auto position_opt = viewport_window->get_position_in_viewport();
        if (!position_opt.has_value()) {
            log_fly_camera->trace("Fly camera has no pointer position in viewport");
            return false;
        }
        constexpr float border   = 32.0f;
        const glm::vec2 position = position_opt.value();
        const erhe::toolkit::Viewport viewport = viewport_window->projection_viewport();
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

Fly_camera_turn_command::Fly_camera_turn_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
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

auto Fly_camera_turn_command::try_call_with_input(
    erhe::commands::Input_arguments& input
) -> bool
{
    const auto value = input.vector2.relative_value;
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

Fly_camera_zoom_command::Fly_camera_zoom_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
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

auto Fly_camera_zoom_command::try_call_with_input(
    erhe::commands::Input_arguments& input
) -> bool
{
    set_ready();

    const auto value = input.vector2.relative_value;
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

Fly_camera_move_command::Fly_camera_move_command(
    erhe::commands::Commands&                        commands,
    Editor_context&                                  context,
    const Variable                                   variable,
    const erhe::toolkit::Simulation_variable_control control,
    const bool                                       active
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
    , m_zoom_command                  {commands, editor_context}
    , m_move_up_active_command        {commands, editor_context, Variable::translate_y, erhe::toolkit::Simulation_variable_control::more, true }
    , m_move_up_inactive_command      {commands, editor_context, Variable::translate_y, erhe::toolkit::Simulation_variable_control::more, false}
    , m_move_down_active_command      {commands, editor_context, Variable::translate_y, erhe::toolkit::Simulation_variable_control::less, true }
    , m_move_down_inactive_command    {commands, editor_context, Variable::translate_y, erhe::toolkit::Simulation_variable_control::less, false}
    , m_move_left_active_command      {commands, editor_context, Variable::translate_x, erhe::toolkit::Simulation_variable_control::less, true }
    , m_move_left_inactive_command    {commands, editor_context, Variable::translate_x, erhe::toolkit::Simulation_variable_control::less, false}
    , m_move_right_active_command     {commands, editor_context, Variable::translate_x, erhe::toolkit::Simulation_variable_control::more, true }
    , m_move_right_inactive_command   {commands, editor_context, Variable::translate_x, erhe::toolkit::Simulation_variable_control::more, false}
    , m_move_forward_active_command   {commands, editor_context, Variable::translate_z, erhe::toolkit::Simulation_variable_control::less, true }
    , m_move_forward_inactive_command {commands, editor_context, Variable::translate_z, erhe::toolkit::Simulation_variable_control::less, false}
    , m_move_backward_active_command  {commands, editor_context, Variable::translate_z, erhe::toolkit::Simulation_variable_control::more, true }
    , m_move_backward_inactive_command{commands, editor_context, Variable::translate_z, erhe::toolkit::Simulation_variable_control::more, false}
{
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
    commands.bind_command_to_key(&m_move_up_active_command,         erhe::toolkit::Key_r, true );
    commands.bind_command_to_key(&m_move_up_inactive_command,       erhe::toolkit::Key_r, false);
    commands.bind_command_to_key(&m_move_down_active_command,       erhe::toolkit::Key_f, true );
    commands.bind_command_to_key(&m_move_down_inactive_command,     erhe::toolkit::Key_f, false);
    commands.bind_command_to_key(&m_move_left_active_command,       erhe::toolkit::Key_a, true );
    commands.bind_command_to_key(&m_move_left_inactive_command,     erhe::toolkit::Key_a, false);
    commands.bind_command_to_key(&m_move_right_active_command,      erhe::toolkit::Key_d, true );
    commands.bind_command_to_key(&m_move_right_inactive_command,    erhe::toolkit::Key_d, false);
    commands.bind_command_to_key(&m_move_forward_active_command,    erhe::toolkit::Key_w, true );
    commands.bind_command_to_key(&m_move_forward_inactive_command,  erhe::toolkit::Key_w, false);
    commands.bind_command_to_key(&m_move_backward_active_command,   erhe::toolkit::Key_s, true );
    commands.bind_command_to_key(&m_move_backward_inactive_command, erhe::toolkit::Key_s, false);

    commands.register_command(&m_turn_command);
    commands.bind_command_to_mouse_drag(&m_turn_command, erhe::toolkit::Mouse_button_left, false);

    commands.register_command(&m_zoom_command);
    commands.bind_command_to_mouse_wheel(&m_zoom_command);

    m_camera_controller = std::make_shared<Frame_controller>();

    m_camera_controller->get_variable(Variable::translate_x).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_y).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_z).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);

    m_rotate_scale_x = config.invert_x ? -1.0f / 1024.0f : 1.0f / 1024.f;
    m_rotate_scale_y = config.invert_y ? -1.0f / 1024.0f : 1.0f / 1024.f;

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            Tool::on_message(message);
        }
    );

    m_turn_command                  .set_host(this);
    m_zoom_command                  .set_host(this);
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
    if (!m_use_viewport_camera) {
        return;
    }

    const auto viewport_window = m_context.viewport_windows->hover_window();
    const auto camera = (viewport_window)
        ? viewport_window->get_camera()
        : std::shared_ptr<erhe::scene::Camera>{};
    const auto* camera_node = camera
        ? camera->get_node()
        : nullptr;

    // TODO This is messy

    if (m_camera_controller->get_node() != camera_node) {
        set_camera(camera.get());
    }
}

void Fly_camera_tool::set_camera(erhe::scene::Camera* const camera)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.

    if (camera != nullptr) {
        auto* scene_root = static_cast<Scene_root*>(camera->get_node()->node_data.host);
        if (scene_root != nullptr) {
            scene_root->scene().update_node_transforms();
        } else {
            log_fly_camera->warn("camera node does not have scene root");
        }
    }

    erhe::scene::Node* node = (camera != nullptr)
        ? camera->get_node()
        : nullptr;
    m_camera_controller->set_node(node);
}

auto Fly_camera_tool::get_camera() const -> erhe::scene::Camera*
{
    return erhe::scene::get_camera(
        m_camera_controller->get_node()
    ).get();
}

void Fly_camera_tool::translation(
    const int tx,
    const int ty,
    const int tz
)
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

void Fly_camera_tool::rotation(
    const int rx,
    const int ry,
    const int rz
)
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

auto Fly_camera_tool::try_move(
    const Variable                                   variable,
    const erhe::toolkit::Simulation_variable_control control,
    const bool                                       active
) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (
        (get_hover_scene_view() == nullptr) &&
        active
    ) {
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

    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (!viewport_window) {
        return false;
    }

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

    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (!viewport_window) {
        return false;
    }

    if (dx != 0.0f) {
        const float value = m_sensitivity * dx * m_rotate_scale_x;
        m_camera_controller->rotate_y.adjust(value);
    }

    if (dy != 0.0f) {
        const float value = m_sensitivity * dy * m_rotate_scale_y;
        m_camera_controller->rotate_x.adjust(value);
    }

    return true;
}

void Fly_camera_tool::update_fixed_step(
    const Time_context& /*time_context*/
)
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    m_camera_controller->update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(
    const Time_context& /*time_context*/
)
{
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

    float speed = m_camera_controller->translate_z.max_delta();

    ImGui::Checkbox   ("Use Viewport Camera", &m_use_viewport_camera);
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);

    m_camera_controller->translate_x.set_max_delta(speed);
    m_camera_controller->translate_y.set_max_delta(speed);
    m_camera_controller->translate_z.set_max_delta(speed);

    // TODO Figure out if this can be re-enabled.
    //      This is currently disabled, because fly camera detaches from
    //      the scene as soon as mouse leaves the viewport, for example
    //      when user moves mouse to *this* ImGui window.
    //
    // auto* camera = get_camera();
    // auto* hover_scene_view = Tool::get_last_hover_scene_view();
    // if (hover_scene_view == nullptr) {
    //     return;
    // }
    // 
    // const auto& scene_root = hover_scene_view->get_scene_root();
    // if (!scene_root) {
    //     return;
    // }
    // if (
    //     scene_root->camera_combo("Camera", camera) &&
    //     !m_use_viewport_camera
    // ) {
    //     set_camera(camera);
    // }

    // \xc2\xb0 is degree symbol UTF-8 encoded
    ImGui::Text("Heading = %.2f\xc2\xb0", simple_degrees(m_camera_controller->get_heading()));
    ImGui::Text("Elevation = %.2f\xc2\xb0", simple_degrees(m_camera_controller->get_elevation()));
#endif
}

}
