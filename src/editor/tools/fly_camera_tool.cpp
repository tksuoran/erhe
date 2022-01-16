#include "tools/fly_camera_tool.hpp"
#include "log.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
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

    const auto relative = context.relative_pointer();
    m_fly_camera_tool.turn_relative(-relative.x, -relative.y);
    return true;
}

auto Fly_camera_move_command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);

    return m_fly_camera_tool.try_move(m_control, m_item, m_active);
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
    m_editor_tools    = get    <Editor_tools>();
    m_pointer_context = get    <Pointer_context>();
    m_scene_root      = require<Scene_root>();
    m_trs_tool        = get    <Trs_tool>();
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
    const auto view = get<Editor_view>();

    view->bind_command_to_key(&m_move_up_active_command,         erhe::toolkit::Key_e, true );
    view->bind_command_to_key(&m_move_up_inactive_command,       erhe::toolkit::Key_e, false);
    view->bind_command_to_key(&m_move_down_active_command,       erhe::toolkit::Key_q, true );
    view->bind_command_to_key(&m_move_down_inactive_command,     erhe::toolkit::Key_q, false);
    view->bind_command_to_key(&m_move_left_active_command,       erhe::toolkit::Key_a, true );
    view->bind_command_to_key(&m_move_left_inactive_command,     erhe::toolkit::Key_a, false);
    view->bind_command_to_key(&m_move_right_active_command,      erhe::toolkit::Key_d, true );
    view->bind_command_to_key(&m_move_right_inactive_command,    erhe::toolkit::Key_d, false);
    view->bind_command_to_key(&m_move_forward_active_command,    erhe::toolkit::Key_w, true );
    view->bind_command_to_key(&m_move_forward_inactive_command,  erhe::toolkit::Key_w, false);
    view->bind_command_to_key(&m_move_backward_active_command,   erhe::toolkit::Key_s, true );
    view->bind_command_to_key(&m_move_backward_inactive_command, erhe::toolkit::Key_s, false);

    view->register_command(&m_turn_command);
    view->bind_command_to_mouse_drag(&m_turn_command, erhe::toolkit::Mouse_button_left);

    m_camera_controller = std::make_shared<Frame_controller>();
}

void Fly_camera_tool::update_camera()
{
    if (!m_use_viewport_camera)
    {
        return;
    }

    auto* window = m_pointer_context->window();
    auto* camera = (window != nullptr)
        ? window->camera()
        : nullptr;
    if (m_camera_controller->get_node() != camera)
    {
        set_camera(camera);
    }
}

void Fly_camera_tool::set_camera(erhe::scene::ICamera* camera)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.
    m_scene_root->scene().update_node_transforms();

    auto* old_host = m_camera_controller->get_node();
    if (old_host != nullptr)
    {
        m_camera_controller->reset();
        old_host->detach(m_camera_controller.get());
    }
    if (camera != nullptr)
    {
        camera->attach(m_camera_controller);
    }
}

auto Fly_camera_tool::get_camera() const -> erhe::scene::ICamera*
{
    return as_icamera(m_camera_controller->get_node());
}

auto Fly_camera_tool::description() -> const char*
{
    return c_description.data();
}

void Fly_camera_tool::translation(const int tx, const int ty, const int tz)
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 16384.0f;
    m_camera_controller->translate_x.adjust(tx / scale);
    m_camera_controller->translate_y.adjust(ty / scale);
    m_camera_controller->translate_z.adjust(tz / scale);
}

void Fly_camera_tool::rotation(const int rx, const int ry, const int rz)
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 16384.0f;
    m_camera_controller->rotate_x.adjust(rx / scale);
    m_camera_controller->rotate_y.adjust(ry / scale);
    m_camera_controller->rotate_z.adjust(rz / scale);
}

auto Fly_camera_tool::try_move(
    const Control         control,
    const Controller_item item,
    const bool            active
) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (
        (m_pointer_context->window() == nullptr) &&
        active
    )
    {
        get<Log_window>()->tail_log("rejected press because no pointer_context window");

        return false;
    }

    auto& controller = m_camera_controller->get_controller(control);
    controller.set(item, active);

    return true;
}

void Fly_camera_tool::turn_relative(const double dx, const double dy)
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (dx != 0.0f)
    {
        const float value = static_cast<float>(m_sensitivity * dx / 1024.0);
        m_camera_controller->rotate_y.adjust(value);
    }

    if (dy != 0.0f)
    {
        const float value = static_cast<float>(m_sensitivity * dy / 1024.0);
        m_camera_controller->rotate_x.adjust(value);
    }
}

void Fly_camera_tool::update_fixed_step(
    const erhe::components::Time_context& /*time_context*/
)
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    m_camera_controller->update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(
    const erhe::components::Time_context& /*time_context*/
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
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    float speed = m_camera_controller->translate_z.max_delta();

    auto* camera = get_camera();
    if (m_scene_root->camera_combo("Camera", camera) && !m_use_viewport_camera)
    {
        set_camera(camera);
    }
    ImGui::Checkbox   ("Use Viewport Camera", &m_use_viewport_camera);
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);

    // \xc2\xb0 is degree symbol UTF-8 encoded
    ImGui::Text("Heading = %.2f\xc2\xb0", simple_degrees(m_camera_controller->heading()));
    ImGui::Text("Elevation = %.2f\xc2\xb0", simple_degrees(m_camera_controller->elevation()));

    m_camera_controller->translate_x.set_max_delta(speed);
    m_camera_controller->translate_y.set_max_delta(speed);
    m_camera_controller->translate_z.set_max_delta(speed);
}

}
