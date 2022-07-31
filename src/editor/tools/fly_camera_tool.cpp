#include "tools/fly_camera_tool.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#include "windows/viewport_window.hpp"
#include "windows/viewport_windows.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/view.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

#if defined(_WIN32) && 0
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

void Fly_camera_turn_command::try_ready(
    erhe::application::Command_context& context
)
{
    auto* viewport_window = as_viewport_window(context.get_window());
    if (
        (state() != erhe::application::State::Inactive) ||
        (viewport_window == nullptr)
    )
    {
        return;
    }

    if (
        viewport_window->get_hover(Hover_entry::tool_slot).valid ||
        viewport_window->get_hover(Hover_entry::rendertarget_slot).valid
    )
    {
        return;
    }

    if (m_fly_camera_tool.try_ready(*viewport_window))
    {
        set_ready(context);
    }
}

auto Fly_camera_tool::try_ready(Viewport_window& viewport_window) -> bool
{
    // Exclude safe border near viewport edges from mouse interaction
    // to filter out viewport window resizing for example.
    if (!viewport_window.position_in_viewport().has_value())
    {
        return false;
    }
    constexpr float   border   = 32.0f;
    const glm::vec2   position = viewport_window.position_in_viewport().value();
    const erhe::scene::Viewport viewport = viewport_window.viewport();
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

auto Fly_camera_turn_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() == erhe::application::State::Ready)
    {
        set_active(context);
    }

    if (state() != erhe::application::State::Active)
    {
        return false;
    }

    auto* viewport_window = as_viewport_window(context.get_window());
    if (viewport_window == nullptr)
    {
        return false;
    }

    const auto relative = context.get_vec2_relative_value();
    m_fly_camera_tool.turn_relative(-relative.x, -relative.y);
    return true;
}

auto Fly_camera_move_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_fly_camera_tool.try_move(m_control, m_item, m_active);
}

Fly_camera_tool::Fly_camera_tool()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
#if defined(_WIN32) && 0
    , m_space_mouse_listener         {*this}
    , m_space_mouse_controller       {m_space_mouse_listener}
#endif
    , m_turn_command                  {*this}
    , m_move_up_active_command        {*this, Control::translate_y, erhe::application::Controller_item::more, true }
    , m_move_up_inactive_command      {*this, Control::translate_y, erhe::application::Controller_item::more, false}
    , m_move_down_active_command      {*this, Control::translate_y, erhe::application::Controller_item::less, true }
    , m_move_down_inactive_command    {*this, Control::translate_y, erhe::application::Controller_item::less, false}
    , m_move_left_active_command      {*this, Control::translate_x, erhe::application::Controller_item::less, true }
    , m_move_left_inactive_command    {*this, Control::translate_x, erhe::application::Controller_item::less, false}
    , m_move_right_active_command     {*this, Control::translate_x, erhe::application::Controller_item::more, true }
    , m_move_right_inactive_command   {*this, Control::translate_x, erhe::application::Controller_item::more, false}
    , m_move_forward_active_command   {*this, Control::translate_z, erhe::application::Controller_item::less, true }
    , m_move_forward_inactive_command {*this, Control::translate_z, erhe::application::Controller_item::less, false}
    , m_move_backward_active_command  {*this, Control::translate_z, erhe::application::Controller_item::more, true }
    , m_move_backward_inactive_command{*this, Control::translate_z, erhe::application::Controller_item::more, false}
{
}

Fly_camera_tool::~Fly_camera_tool() noexcept
{
#if defined(_WIN32) && 0
    m_space_mouse_listener.set_active(false);
#endif
}

void Fly_camera_tool::declare_required_components()
{
    m_editor_tools = require<Tools>();

    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<erhe::application::View>();
}

void Fly_camera_tool::initialize_component()
{
#if defined(_WIN32) && 0
    m_space_mouse_listener.set_active(true);
#endif

    m_editor_tools->register_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto& config = get<erhe::application::Configuration>()->camera_controls;
    const auto& view   = get<erhe::application::View>();

    // TODO these commands should be registered
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

    m_camera_controller->get_controller(Control::translate_x).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_controller(Control::translate_y).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_controller(Control::translate_z).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);

    m_rotate_scale_x = config.invert_x ? -1.0f / 1024.0f : 1.0f / 1024.f;
    m_rotate_scale_y = config.invert_y ? -1.0f / 1024.0f : 1.0f / 1024.f;
}

void Fly_camera_tool::post_initialize()
{
    m_editor_scenes    = get<Editor_scenes   >();
    m_trs_tool         = get<Trs_tool        >();
    m_viewport_windows = get<Viewport_windows>();
}

void Fly_camera_tool::update_camera()
{
    if (!m_use_viewport_camera)
    {
        return;
    }

    auto* window = m_viewport_windows->hover_window();
    auto* camera = (window != nullptr)
        ? window->camera()
        : nullptr;
    if (m_camera_controller->get_node() != camera)
    {
        set_camera(camera);
    }
}

void Fly_camera_tool::set_camera(erhe::scene::Camera* camera)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.

    if (camera != nullptr)
    {
        auto* scene_root = reinterpret_cast<Scene_root*>(camera->node_data.host);
        if (scene_root != nullptr)
        {
            scene_root->scene().update_node_transforms();
        }
        else
        {
            log_fly_camera->warn("camera node does not have scene root");
        }
    }

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

auto Fly_camera_tool::get_camera() const -> erhe::scene::Camera*
{
    return as_camera(m_camera_controller->get_node());
}

auto Fly_camera_tool::description() -> const char*
{
    return c_title.data();
}

void Fly_camera_tool::translation(
    const int tx,
    const int ty,
    const int tz
)
{
    if (!m_camera_controller)
    {
        return;
    }
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->translate_x.adjust(tx / scale);
    m_camera_controller->translate_y.adjust(ty / scale);
    m_camera_controller->translate_z.adjust(tz / scale);
}

void Fly_camera_tool::rotation(
    const int rx,
    const int ry,
    const int rz
)
{
    if (!m_camera_controller)
    {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->rotate_x.adjust(rx / scale);
    m_camera_controller->rotate_y.adjust(ry / scale);
    m_camera_controller->rotate_z.adjust(rz / scale);
}

auto Fly_camera_tool::try_move(
    const Control                            control,
    const erhe::application::Controller_item item,
    const bool                               active
) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (
        (m_viewport_windows->hover_window() == nullptr) &&
        active
    )
    {
        log_fly_camera->warn("rejected press because no viewport window");

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
        const float value = static_cast<float>(m_sensitivity * dx * m_rotate_scale_x);
        m_camera_controller->rotate_y.adjust(value);
    }

    if (dy != 0.0f)
    {
        const float value = static_cast<float>(m_sensitivity * dy * m_rotate_scale_y);
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
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    float speed = m_camera_controller->translate_z.max_delta();

    auto* camera = get_camera();
    const auto scene_root = m_editor_scenes->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    if (
        m_editor_scenes->get_scene_root()->camera_combo(
            "Camera",
            camera
        ) &&
        !m_use_viewport_camera
    )
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
#endif
}

}
