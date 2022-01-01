#include "tools/fly_camera_tool.hpp"
#include "log.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "scene/frame_controller.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "windows/log_window.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/view.hpp"

#include <imgui.h>

namespace editor
{

using namespace std;
using namespace erhe::scene;
using namespace erhe::toolkit;

#ifdef _WIN32
Fly_camera_space_mouse_listener::Fly_camera_space_mouse_listener(
    Fly_camera_tool& fly_camera_tool
)
    : m_fly_camera_tool{fly_camera_tool}
{
}

Fly_camera_space_mouse_listener::~Fly_camera_space_mouse_listener()
{
}

auto Fly_camera_space_mouse_listener::is_active() -> bool
{
    return m_is_active;
}

void Fly_camera_space_mouse_listener::set_active(const bool value)
{
    m_is_active = value;
}

void Fly_camera_space_mouse_listener::on_translation(const int tx, const int ty, const int tz)
{
    m_fly_camera_tool.translation(tx, -tz, ty);
}

void Fly_camera_space_mouse_listener::on_rotation(const int rx, const int ry, const int rz)
{
    m_fly_camera_tool.rotation(rx, -rz, ry);
}

void Fly_camera_space_mouse_listener::on_button(const int)
{
}
#endif

void Fly_camera_turn_command::try_ready(Command_context& context)
{
    if (
        (state() != State::Inactive) ||
        (context.viewport_window() == nullptr) ||
        context.hovering_over_tool()
    )
    {
        return;
    }

    if (m_fly_camera_tool.try_ready())
    {
        set_ready(context);
    }
}

auto Fly_camera_tool::try_ready() -> bool
{
    // Exclude safe border near viewport edges from mouse interaction
    // to filter out viewport window resizing for example.
    constexpr float   border   = 32.0f;
    const auto        position = m_pointer_context->position_in_viewport_window().value();
    const auto* const window   = m_pointer_context->window();
    const auto        viewport = window->viewport();
    if (
        (position.x <  border) ||
        (position.y <  border) ||
        (position.x >= viewport.width  - border) ||
        (position.y >= viewport.height - border)
    )
    {
        return false;
    }

    return true;
}

auto Fly_camera_turn_command::try_call(Command_context& context) -> bool
{
    if (state() == State::Ready)
    {
        set_active(context);
    }

    if (state() != State::Active)
    {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    m_fly_camera_tool.turn_relative(io.MouseDelta.x, io.MouseDelta.y);
    return true;
}

auto Fly_camera_move_command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);

    m_fly_camera_tool.move(m_control, m_item, m_active);
    return true;
}

Fly_camera_tool::Fly_camera_tool()
    : erhe::components::Component     {c_name}
    , Imgui_window                    {c_description}
    , m_turn_command                  {*this}
    , m_move_up_active_command        {*this, Control::translate_y, Controller_item::more, true}
    , m_move_up_inactive_command      {*this, Control::translate_y, Controller_item::more, false}
    , m_move_down_active_command      {*this, Control::translate_y, Controller_item::less, true}
    , m_move_down_inactive_command    {*this, Control::translate_y, Controller_item::less, false}
    , m_move_left_active_command      {*this, Control::translate_x, Controller_item::less, true}
    , m_move_left_inactive_command    {*this, Control::translate_x, Controller_item::less, false}
    , m_move_right_active_command     {*this, Control::translate_x, Controller_item::more, true}
    , m_move_right_inactive_command   {*this, Control::translate_x, Controller_item::more, false}
    , m_move_forward_active_command   {*this, Control::translate_z, Controller_item::less, true}
    , m_move_forward_inactive_command {*this, Control::translate_z, Controller_item::less, false}
    , m_move_backward_active_command  {*this, Control::translate_z, Controller_item::more, true}
    , m_move_backward_inactive_command{*this, Control::translate_z, Controller_item::more, false}

#ifdef _WIN32
    , m_space_mouse_listener     {*this}
    , m_space_mouse_controller   {m_space_mouse_listener}
#endif
{
}

Fly_camera_tool::~Fly_camera_tool()
{
#ifdef _WIN32
    m_space_mouse_listener.set_active(false);
#endif
}

void Fly_camera_tool::connect()
{
    m_editor_tools    = get<Editor_tools>();
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = require<Scene_root>();
    require<Scene_builder>();
    m_trs_tool        = get<Trs_tool>();
}

auto Fly_camera_tool::can_use_keyboard() const -> bool
{
    return m_pointer_context->window() != nullptr;
}

void Fly_camera_tool::initialize_component()
{
#ifdef _WIN32
    m_space_mouse_listener.set_active(true);
#endif

    m_editor_tools->register_tool(this);
    auto* view = get<Editor_view>();

    using Keycode = erhe::toolkit::Keycode;
    view->bind_command_to_key(&m_move_up_active_command,         Keycode::Key_e, true );
    view->bind_command_to_key(&m_move_up_inactive_command,       Keycode::Key_e, false);
    view->bind_command_to_key(&m_move_down_active_command,       Keycode::Key_q, true );
    view->bind_command_to_key(&m_move_down_inactive_command,     Keycode::Key_q, false);
    view->bind_command_to_key(&m_move_left_active_command,       Keycode::Key_a, true );
    view->bind_command_to_key(&m_move_left_inactive_command,     Keycode::Key_a, false);
    view->bind_command_to_key(&m_move_right_active_command,      Keycode::Key_d, true );
    view->bind_command_to_key(&m_move_right_inactive_command,    Keycode::Key_d, false);
    view->bind_command_to_key(&m_move_forward_active_command,    Keycode::Key_w, true );
    view->bind_command_to_key(&m_move_forward_inactive_command,  Keycode::Key_w, false);
    view->bind_command_to_key(&m_move_backward_active_command,   Keycode::Key_s, true ); 
    view->bind_command_to_key(&m_move_backward_inactive_command, Keycode::Key_s, false);

    view->register_command(&m_turn_command);
    view->bind_command_to_mouse_drag(&m_turn_command, Mouse_button_left);
}

void Fly_camera_tool::set_camera(erhe::scene::ICamera* camera)
{
    // set_frame() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.
    m_scene_root->scene().update_node_transforms();

    m_camera_controller.set_node(camera);
}

auto Fly_camera_tool::get_camera() const -> erhe::scene::ICamera*
{
    return as_icamera(m_camera_controller.get_node());
}

auto Fly_camera_tool::description() -> const char*
{
    return c_description.data();
}

void Fly_camera_tool::translation(const int tx, const int ty, const int tz)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 16384.0f;
    m_camera_controller.translate_x.adjust(tx / scale);
    m_camera_controller.translate_y.adjust(ty / scale);
    m_camera_controller.translate_z.adjust(tz / scale);
}

void Fly_camera_tool::rotation(const int rx, const int ry, const int rz)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 16384.0f;
    m_camera_controller.rotate_x.adjust(rx / scale);
    m_camera_controller.rotate_y.adjust(ry / scale);
    m_camera_controller.rotate_z.adjust(rz / scale);
}

void Fly_camera_tool::move(
    const Control         control,
    const Controller_item item, 
    const bool            active
)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    auto& controller = m_camera_controller.get_controller(control);
    controller.set(item, active);
}


void Fly_camera_tool::turn_relative(const double dx, const double dy)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (dx != 0.0f)
    {
        const float value = static_cast<float>(m_sensitivity * dx / 1024.0);
        m_camera_controller.rotate_y.adjust(value);
    }

    if (dy != 0.0f)
    {
        const float value = static_cast<float>(m_sensitivity * dy / 1024.0);
        m_camera_controller.rotate_x.adjust(value);
    }
}

void Fly_camera_tool::update_fixed_step(
    const erhe::components::Time_context& /*time_context*/
)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    m_camera_controller.update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(
    const erhe::components::Time_context& /*time_context*/
)
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    m_camera_controller.update();
}

void Fly_camera_tool::imgui()
{
    std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    float speed = m_camera_controller.translate_z.max_delta();

    auto* camera = get_camera();
    if (m_scene_root->camera_combo("Camera", camera))
    {
        set_camera(camera);
    }
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);

    m_camera_controller.translate_x.set_max_delta(speed);
    m_camera_controller.translate_y.set_max_delta(speed);
    m_camera_controller.translate_z.set_max_delta(speed);
}

}
