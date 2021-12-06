#include "tools/fly_camera_tool.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"

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

Fly_camera_tool::Fly_camera_tool()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
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

auto Fly_camera_tool::state() const -> State
{
    return m_state;
}

void Fly_camera_tool::connect()
{
    m_pointer_context = get<Pointer_context>();
    m_scene_root     = require<Scene_root>();
    require<Scene_manager>();
}

void Fly_camera_tool::initialize_component()
{
#ifdef _WIN32
    m_space_mouse_listener.set_active(true);
#endif

    get<Editor_tools>()->register_tool(this);
}

void Fly_camera_tool::set_camera(erhe::scene::ICamera* camera)
{
    // set_frame() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.
    m_scene_root->scene().update_node_transforms();

    m_camera_controller.set_frame(camera);
}

auto Fly_camera_tool::camera() const -> erhe::scene::ICamera*
{
    return reinterpret_cast<erhe::scene::ICamera*>(m_camera_controller.node());
}

auto Fly_camera_tool::description() -> const char*
{
    return c_description.data();
}

void Fly_camera_tool::translation(const int tx, const int ty, const int tz)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    constexpr float scale = 16384.0f;
    m_camera_controller.translate_x.adjust(tx / scale);
    m_camera_controller.translate_y.adjust(ty / scale);
    m_camera_controller.translate_z.adjust(tz / scale);
}

void Fly_camera_tool::rotation(const int rx, const int ry, const int rz)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    constexpr float scale = 16384.0f;
    m_camera_controller.rotate_x.adjust(rx / scale);
    m_camera_controller.rotate_y.adjust(ry / scale);
    m_camera_controller.rotate_z.adjust(rz / scale);
}

void Fly_camera_tool::x_pos_control(const bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.translate_x.set_more(pressed);
}

void Fly_camera_tool::x_neg_control(const bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.translate_x.set_less(pressed);
}

void Fly_camera_tool::y_pos_control(const bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.translate_y.set_more(pressed);
}

void Fly_camera_tool::y_neg_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_y.set_less(pressed);
}

void Fly_camera_tool::z_neg_control(const bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.translate_z.set_less(pressed);
}

void Fly_camera_tool::z_pos_control(const bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.translate_z.set_more(pressed);
}

auto Fly_camera_tool::tool_update() -> bool
{
    ERHE_PROFILE_FUNCTION

    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    const bool   hovering_over_tool = m_pointer_context->hovering_over_tool();
    const bool   left_pressed       = m_pointer_context->mouse_button_pressed(Mouse_button_left);
    const bool   left_released      = m_pointer_context->mouse_button_released(Mouse_button_left);
    const double x_delta            = m_pointer_context->mouse_x() - m_mouse_x;
    const double y_delta            = m_pointer_context->mouse_y() - m_mouse_y;
    const bool   mouse_moved        = (std::abs(x_delta) > 0.5) || (std::abs(y_delta) > 0.5);
    if (
        (m_state == State::Passive) &&
        left_pressed &&
        !hovering_over_tool
    )
    {
        if (!mouse_moved)
        {
            return false;
        }
        return begin();
    }

    if (
        (m_state != State::Passive) &&
        left_released
    )
    {
        return end();
    }

    if (
        (m_state == State::Ready) &&
        mouse_moved
    )
    {
        log_tools.trace("Fly camera state = Active\n");
        m_state = State::Active;
    }

    if (m_state != State::Active)
    {
        // We might be ready, but not consuming event yet
        return false;
    }

    if (x_delta != 0.0)
    {
        const float value = static_cast<float>(m_sensitivity * x_delta / 1024.0);
        m_camera_controller.rotate_y.adjust(value);
        m_mouse_x = m_pointer_context->mouse_x();
    }

    if (y_delta != 0.0)
    {
        const float value = static_cast<float>(m_sensitivity * y_delta / 1024.0);
        m_camera_controller.rotate_x.adjust(value);
        m_mouse_y = m_pointer_context->mouse_y();
    }

    return true;
}

auto Fly_camera_tool::begin() -> bool
{
    if (!m_pointer_context->pointer_in_content_area())
    {
        return false;
    }
    if (
        m_pointer_context->window() == nullptr ||
        !m_pointer_context->position_in_viewport_window().has_value()
    )
    {
        return false;
    }

    // Reject drags near edge of viewport.
    // This avoids window resize being misinterpreted as drag.
    constexpr float   border   = 32.0f;
    const auto        position = m_pointer_context->position_in_viewport_window().value();
    const auto* const window   = m_pointer_context->window();
    if (window == nullptr)
    {
        return false;
    }

    const auto viewport = window->viewport();
    if (
        (position.x <  border) ||
        (position.y <  border) ||
        (position.x >= viewport.width  - border) ||
        (position.y >= viewport.height - border)
    )
    {
        return false;
    }

    m_state   = State::Ready;
    m_mouse_x = m_pointer_context->mouse_x();
    m_mouse_y = m_pointer_context->mouse_y();
    return true;
}

auto Fly_camera_tool::end() -> bool
{
    if (m_state == State::Passive)
    {
        return false;
    }

    const bool consume_event = (m_state == State::Active);
    cancel_ready();
    return consume_event;
}

void Fly_camera_tool::cancel_ready()
{
    log_tools.trace("Fly camera state = Inactive\n");
    m_state = State::Passive;
}

void Fly_camera_tool::update_fixed_step(
    const erhe::components::Time_context& /*time_context*/
)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(
    const erhe::components::Time_context& /*time_context*/
)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.update();
}

void Fly_camera_tool::imgui()
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    float speed = m_camera_controller.translate_z.max_delta();

    ImGui::Begin      (c_description.data());
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);
    ImGui::End        ();

    m_camera_controller.translate_x.set_max_delta(speed);
    m_camera_controller.translate_y.set_max_delta(speed);
    m_camera_controller.translate_z.set_max_delta(speed);
}

}
