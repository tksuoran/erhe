#include "tools/fly_camera_tool.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"

#include "imgui.h"

namespace editor
{

using namespace std;
using namespace erhe::scene;
using namespace erhe::toolkit;

Fly_camera_space_mouse_listener::Fly_camera_space_mouse_listener(Fly_camera_tool& fly_camera_tool)
    : m_fly_camera_tool{fly_camera_tool}
{
}

Fly_camera_space_mouse_listener::~Fly_camera_space_mouse_listener()
{
}

bool Fly_camera_space_mouse_listener::is_active()
{
    return m_is_active;
}

void Fly_camera_space_mouse_listener::set_active(bool value)
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

Fly_camera_tool::Fly_camera_tool()
    : erhe::components::Component{c_name}
    , m_space_mouse_listener     {*this}
    , m_space_mouse_controller   {m_space_mouse_listener}
{
}

Fly_camera_tool::~Fly_camera_tool()
{
    m_space_mouse_listener.set_active(false);
}

auto Fly_camera_tool::state() const -> State
{
    return m_state;
}

void Fly_camera_tool::connect()
{
    m_scene_root = require<Scene_root>();
    require<Scene_manager>();
}

void Fly_camera_tool::initialize_component()
{
    auto scene_manager = get<Scene_manager>();
    if (!scene_manager)
    {
        return;
    }

    auto camera = scene_manager->get_view_camera();
    if (!camera)
    {
        return;
    }

    m_camera_controller.set_frame(camera->node().get());
    m_space_mouse_listener.set_active(true);

    get<Editor_tools>()->register_tool(this);
}

auto Fly_camera_tool::description() -> const char*
{
    return c_name;
}

void Fly_camera_tool::translation(int tx, int ty, int tz)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    constexpr const float scale = 16384.0f;
    m_camera_controller.translate_x.adjust(tx / scale);
    m_camera_controller.translate_y.adjust(ty / scale);
    m_camera_controller.translate_z.adjust(tz / scale);
}

void Fly_camera_tool::rotation(int rx, int ry, int rz)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    constexpr const float scale = 16384.0f;
    m_camera_controller.rotate_x.adjust(rx / scale);
    m_camera_controller.rotate_y.adjust(ry / scale);
    m_camera_controller.rotate_z.adjust(rz / scale);
}

void Fly_camera_tool::x_pos_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_x.set_more(pressed);
}

void Fly_camera_tool::x_neg_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_x.set_less(pressed);
}

void Fly_camera_tool::y_pos_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_y.set_more(pressed);
}

void Fly_camera_tool::y_neg_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_y.set_less(pressed);
}

void Fly_camera_tool::z_neg_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_z.set_less(pressed);
}

void Fly_camera_tool::z_pos_control(bool pressed)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);
    m_camera_controller.translate_z.set_more(pressed);
}

auto Fly_camera_tool::update(Pointer_context& pointer_context) -> bool
{
    ZoneScoped;

    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    if ((m_state == State::Passive) &&
        pointer_context.mouse_button[Mouse_button_left].pressed &&
        !pointer_context.hover_tool)
    {
        return begin(pointer_context);
    }
    if ((m_state != State::Passive) && pointer_context.mouse_button[Mouse_button_left].released)
    {
        return end(pointer_context);
    }
    if ((m_state == State::Ready) && pointer_context.mouse_moved)
    {
        log_tools.trace("Fly camera state = Active\n");
        m_state = State::Active;
    }
    if (m_state != State::Active)
    {
        // We might be ready, but not consuming event yet
        return false;
    }

    double x_delta = pointer_context.mouse_x - m_mouse_x;
    double y_delta = pointer_context.mouse_y - m_mouse_y;

    if (x_delta != 0.0)
    {
        float value = static_cast<float>(m_sensitivity * x_delta / 1024.0);
        m_camera_controller.rotate_y.adjust(value);
        m_mouse_x = pointer_context.mouse_x;
    }

    if (y_delta != 0.0)
    {
        float value = static_cast<float>(m_sensitivity * y_delta / 1024.0);
        m_camera_controller.rotate_x.adjust(value);
        m_mouse_y = pointer_context.mouse_y;
    }

    return true;
}

auto Fly_camera_tool::begin(Pointer_context& pointer_context) -> bool
{
    if (!pointer_context.pointer_in_content_area())
    {
        return false;
    }
    if (!pointer_context.scene_view_focus)
    {
        return false;
    }

    // Reject drags near edge of viewport.
    // This avoids window resize being misinterpreted as drag.
    static constexpr const float border = 32.0f;
    if ((pointer_context.pointer_x < border) ||
        (pointer_context.pointer_y < border) ||
        (pointer_context.pointer_x >= pointer_context.viewport.width  - border) ||
        (pointer_context.pointer_y >= pointer_context.viewport.height - border))
    {
        return false;
    }

    log_tools.trace("Fly camera state = Ready\n");
    m_state   = State::Ready;
    m_mouse_x = pointer_context.mouse_x;
    m_mouse_y = pointer_context.mouse_y;
    return true;
}

auto Fly_camera_tool::end(Pointer_context&) -> bool
{
    if (m_state == State::Passive)
    {
        return false;
    }

    bool consume_event = m_state == State::Active;
    cancel_ready();
    return consume_event;
}

void Fly_camera_tool::cancel_ready()
{
    log_tools.trace("Fly camera state = Inactive\n");
    m_state = State::Passive;
}

void Fly_camera_tool::update_fixed_step(const erhe::components::Time_context& time_context)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.update_fixed_step();
}

void Fly_camera_tool::update_once_per_frame(const erhe::components::Time_context& time_context)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    m_camera_controller.update();
}

void Fly_camera_tool::window(Pointer_context&)
{
    std::lock_guard<std::mutex> lock_fly_camera(m_mutex);

    float speed = m_camera_controller.translate_z.max_delta();

    ImGui::Begin      ("Fly Camera");
    ImGui::SliderFloat("Sensitivity", &m_sensitivity, 0.2f,   2.0f);
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);
    ImGui::End        ();

    m_camera_controller.translate_z.set_max_delta(speed);
}

}
