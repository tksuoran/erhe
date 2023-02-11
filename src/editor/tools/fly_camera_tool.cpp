#include "tools/fly_camera_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "windows/imgui_viewport_window.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/view.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
Fly_camera_space_mouse_listener::Fly_camera_space_mouse_listener() = default;

Fly_camera_space_mouse_listener::~Fly_camera_space_mouse_listener() noexcept = default;

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
    if (g_fly_camera_tool == nullptr)
    {
        return;
    }
    g_fly_camera_tool->translation(tx, -tz, ty);
}

void Fly_camera_space_mouse_listener::on_rotation(const int rx, const int ry, const int rz)
{
    if (g_fly_camera_tool == nullptr)
    {
        return;
    }
    g_fly_camera_tool->rotation(rx, -rz, ry);
}

void Fly_camera_space_mouse_listener::on_button(const int)
{
}
#endif

void Fly_camera_turn_command::try_ready()
{
    if (g_fly_camera_tool == nullptr)
    {
        return;
    }

    if (g_fly_camera_tool->try_ready())
    {
        set_ready();
    }
}

auto Fly_camera_tool::try_ready() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return false;
    }

    if (
        scene_view->get_hover(Hover_entry::tool_slot        ).valid ||
        scene_view->get_hover(Hover_entry::rendertarget_slot).valid
    )
    {
        return false;
    }

    const Viewport_window* viewport_window = scene_view->as_viewport_window();
    if (viewport_window != nullptr)
    {
        // Exclude safe border near viewport edges from mouse interaction
        // to filter out viewport window resizing for example.
        const auto position_opt = viewport_window->get_position_in_viewport();
        if (!position_opt.has_value())
        {
            return false;
        }
        constexpr float border   = 32.0f;
        const glm::vec2 position = position_opt.value();
        const erhe::scene::Viewport viewport = viewport_window->projection_viewport();
        if (
            (position.x <  border) ||
            (position.y <  border) ||
            (position.x >= viewport.width  - border) ||
            (position.y >= viewport.height - border)
        )
        {
            return false;
        }
    }

    return true;
}

Fly_camera_turn_command::Fly_camera_turn_command()
    : Command{"Fly_camera.turn_camera"}
{
}

auto Fly_camera_turn_command::try_call_with_input(
    erhe::application::Input_arguments& input
) -> bool
{
    if (g_fly_camera_tool == nullptr)
    {
        return false;
    }

    const auto value = input.vector2.relative_value;
    if (get_command_state() == erhe::application::State::Ready)
    {
        if (g_fly_camera_tool->get_hover_scene_view() == nullptr)
        {
            set_inactive();
            return false;
        }
        if ((value.x != 0.0f) || (value.y != 0.0f))
        {
            set_active();
        }
    }

    if (get_command_state() != erhe::application::State::Active)
    {
        return false;
    }

    g_fly_camera_tool->turn_relative(-value.x, -value.y);
    return true;
}

Fly_camera_move_command::Fly_camera_move_command(
    const Control                            control,
    const erhe::application::Controller_item item,
    const bool                               active
)
    : Command  {"Fly_camera.move"}
    , m_control{control          }
    , m_item   {item             }
    , m_active {active           }
{
}

auto Fly_camera_move_command::try_call() -> bool
{
    if (g_fly_camera_tool == nullptr)
    {
        return false;
    }

    return g_fly_camera_tool->try_move(m_control, m_item, m_active);
}

Fly_camera_tool* g_fly_camera_tool{nullptr};

Fly_camera_tool::Fly_camera_tool()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
    , m_move_up_active_command        {Control::translate_y, erhe::application::Controller_item::more, true }
    , m_move_up_inactive_command      {Control::translate_y, erhe::application::Controller_item::more, false}
    , m_move_down_active_command      {Control::translate_y, erhe::application::Controller_item::less, true }
    , m_move_down_inactive_command    {Control::translate_y, erhe::application::Controller_item::less, false}
    , m_move_left_active_command      {Control::translate_x, erhe::application::Controller_item::less, true }
    , m_move_left_inactive_command    {Control::translate_x, erhe::application::Controller_item::less, false}
    , m_move_right_active_command     {Control::translate_x, erhe::application::Controller_item::more, true }
    , m_move_right_inactive_command   {Control::translate_x, erhe::application::Controller_item::more, false}
    , m_move_forward_active_command   {Control::translate_z, erhe::application::Controller_item::less, true }
    , m_move_forward_inactive_command {Control::translate_z, erhe::application::Controller_item::less, false}
    , m_move_backward_active_command  {Control::translate_z, erhe::application::Controller_item::more, true }
    , m_move_backward_inactive_command{Control::translate_z, erhe::application::Controller_item::more, false}
#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
    , m_space_mouse_listener  {}
    , m_space_mouse_controller{m_space_mouse_listener}
#endif
{
}

Fly_camera_tool::~Fly_camera_tool() noexcept
{
    ERHE_VERIFY(g_fly_camera_tool == nullptr);
}

void Fly_camera_tool::deinitialize_component()
{
    ERHE_VERIFY(g_fly_camera_tool == this);
    m_turn_command                  .set_host(nullptr);
    m_move_up_active_command        .set_host(nullptr);
    m_move_up_inactive_command      .set_host(nullptr);
    m_move_down_active_command      .set_host(nullptr);
    m_move_down_inactive_command    .set_host(nullptr);
    m_move_left_active_command      .set_host(nullptr);
    m_move_left_inactive_command    .set_host(nullptr);
    m_move_right_active_command     .set_host(nullptr);
    m_move_right_inactive_command   .set_host(nullptr);
    m_move_forward_active_command   .set_host(nullptr);
    m_move_forward_inactive_command .set_host(nullptr);
    m_move_backward_active_command  .set_host(nullptr);
    m_move_backward_inactive_command.set_host(nullptr);
    m_camera_controller.reset();
#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
    m_space_mouse_listener.set_active(false);
#endif
    g_fly_camera_tool = nullptr;
}

void Fly_camera_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Tools>();
}

void Fly_camera_tool::initialize_component()
{
    ERHE_VERIFY(g_fly_camera_tool == nullptr);

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
    m_space_mouse_listener.set_active(true);
#endif

    auto ini = erhe::application::get_ini("erhe.ini", "camera_controls");
    ini->get("invert_x",           config.invert_x);
    ini->get("invert_y",           config.invert_y);
    ini->get("velocity_damp",      config.velocity_damp);
    ini->get("velocity_max_delta", config.velocity_max_delta);
    ini->get("sensitivity",        config.sensitivity);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::background);
    g_tools->register_tool(this);
    erhe::application::g_imgui_windows->register_imgui_window(this, "fly_camera");

    auto& commands = *erhe::application::g_commands;

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

    m_camera_controller = std::make_shared<Frame_controller>();

    m_camera_controller->get_controller(Control::translate_x).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_controller(Control::translate_y).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_controller(Control::translate_z).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);

    m_rotate_scale_x = config.invert_x ? -1.0f / 1024.0f : 1.0f / 1024.f;
    m_rotate_scale_y = config.invert_y ? -1.0f / 1024.0f : 1.0f / 1024.f;

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            Tool::on_message(message);
        }
    );

    m_turn_command                  .set_host(this);
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

    g_fly_camera_tool = this;
}

void Fly_camera_tool::update_camera()
{
    if (!m_use_viewport_camera)
    {
        return;
    }

    const auto viewport_window = g_viewport_windows->hover_window();
    const auto camera = (viewport_window)
        ? viewport_window->get_camera()
        : std::shared_ptr<erhe::scene::Camera>{};
    const auto* camera_node = camera
        ? camera->get_node()
        : nullptr;

    // TODO This is messy

    if (m_camera_controller->get_node() != camera_node)
    {
        set_camera(camera.get());
    }
}

void Fly_camera_tool::set_camera(erhe::scene::Camera* const camera)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.

    if (camera != nullptr)
    {
        auto* scene_root = reinterpret_cast<Scene_root*>(camera->get_node()->node_data.host);
        if (scene_root != nullptr)
        {
            scene_root->scene().update_node_transforms();
        }
        else
        {
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
    if (!m_camera_controller)
    {
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
    if (!m_camera_controller)
    {
        return;
    }

    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->rotate_x.adjust(m_sensitivity * static_cast<float>(rx) / scale);
    m_camera_controller->rotate_y.adjust(m_sensitivity * static_cast<float>(ry) / scale);
    m_camera_controller->rotate_z.adjust(m_sensitivity * static_cast<float>(rz) / scale);
}

auto Fly_camera_tool::try_move(
    const Control                            control,
    const erhe::application::Controller_item item,
    const bool                               active
) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    if (
        (g_viewport_windows->hover_window() == nullptr) &&
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

auto Fly_camera_tool::turn_relative(const float dx, const float dy) -> bool
{
    const std::lock_guard<std::mutex> lock_fly_camera{m_mutex};

    const auto viewport_window = g_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return false;
    }

    if (dx != 0.0f)
    {
        const float value = m_sensitivity * dx * m_rotate_scale_x;
        m_camera_controller->rotate_y.adjust(value);
    }

    if (dy != 0.0f)
    {
        const float value = m_sensitivity * dy * m_rotate_scale_y;
        m_camera_controller->rotate_x.adjust(value);
    }

    return true;
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
    const auto& scene_root = g_editor_scenes->get_current_scene_root();
    if (!scene_root)
    {
        return;
    }

    if (
        scene_root->camera_combo(
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
