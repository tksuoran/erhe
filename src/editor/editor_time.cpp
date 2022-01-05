#include "editor_time.hpp"
#include "application.hpp"
#include "scene/scene_root.hpp"
#include "window.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"


namespace editor {

using namespace std;


Editor_time::Editor_time()
    : erhe::components::Component{c_name}
{
}

Editor_time::~Editor_time() = default;

void Editor_time::connect()
{
    m_application = get<Application>();
    m_scene_root  = get<Scene_root >();
}

void Editor_time::start_time()
{
    m_current_time = chrono::steady_clock::now();
}

void Editor_time::update()
{
    ERHE_PROFILE_FUNCTION

    const auto new_time   = chrono::steady_clock::now();
    const auto duration   = new_time - m_current_time;
    double     frame_time = chrono::duration<double, ratio<1>>(duration).count();

    if (frame_time > 0.25)
    {
        frame_time = 0.25;
    }

    m_current_time = new_time;
    m_time_accumulator += frame_time;
    const double dt = 1.0 / 100.0;
    while (m_time_accumulator >= dt)
    {
        update_fixed_step(erhe::components::Time_context{dt, m_time, m_frame_number});
        m_time_accumulator -= dt;
        m_time += dt;
    }

    update_once_per_frame(erhe::components::Time_context{dt, m_time, m_frame_number});
    m_scene_root->scene().update_node_transforms();

    ERHE_PROFILE_FRAME_END
}

void Editor_time::update_fixed_step(const erhe::components::Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION

    for (auto update : m_components->fixed_step_updates)
    {
        update->update_fixed_step(time_context);
    }
}

void Editor_time::update_once_per_frame(const erhe::components::Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION

    for (auto update : m_components->once_per_frame_updates)
    {
        update->update_once_per_frame(time_context);
    }
    ++m_frame_number;
}

auto Editor_time::time() const -> double
{
    return m_time;
}

}  // namespace editor