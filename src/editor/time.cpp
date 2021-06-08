#include "time.hpp"
#include "rendering.hpp"
#include "gl_context_provider.hpp"

#include "application.hpp"
#include "log.hpp"

#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "backends/imgui_impl_glfw.h"


namespace editor {

using namespace std;

static thread_local erhe::toolkit::Context_window* s_worker_thread_context = nullptr;

Editor_time::Editor_time()
    : erhe::components::Component{c_name}
{
}

Editor_time::~Editor_time() = default;

void Editor_time::connect()
{
    m_editor_rendering = get<Editor_rendering>();
    m_application      = get<Application     >();
}

void Editor_time::start_time()
{
    m_current_time = chrono::steady_clock::now();
}

static constexpr const char* c_swap_buffers = "swap buffers";


void Editor_time::update()
{
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
    m_editor_rendering->render(m_time);

    {
        ZoneScopedN(c_swap_buffers);
        m_application->get_context_window()->swap_buffers();
    }

    //if (m_trigger_capture)
    //{
    //    m_application->end_renderdoc_capture();
    //    m_trigger_capture = false;
    //}

    FrameMark;
    TracyGpuCollect
}

void Editor_time::update_fixed_step(const erhe::components::Time_context& time_context)
{
    ZoneScoped;

    for (auto update : m_components->fixed_step_updates)
    {
        update->update_fixed_step(time_context);
    }
}

void Editor_time::update_once_per_frame(const erhe::components::Time_context& time_context)
{
    ZoneScoped;

    for (auto update : m_components->once_per_frame_updates)
    {
        update->update_once_per_frame(time_context);
    }
    ++m_frame_number;
}

}  // namespace editor


// BaseRenderer UI -> make it sub window
// Node -
// Transform
// Selection
