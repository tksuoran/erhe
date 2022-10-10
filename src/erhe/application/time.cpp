#include "erhe/application/time.hpp"
#include "erhe/application/application.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/window.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::application {

Time::Time()
    : erhe::components::Component{c_type_name}
{
}

Time::~Time() noexcept
{
}

void Time::start_time()
{
    m_current_time = std::chrono::steady_clock::now();
}

void Time::update()
{
    ERHE_PROFILE_FUNCTION

    const auto new_time   = std::chrono::steady_clock::now();
    const auto duration   = new_time - m_current_time;
    double     frame_time = std::chrono::duration<double, std::ratio<1>>(duration).count();

    if (frame_time > 0.25)
    {
        frame_time = 0.25;
    }

    m_current_time = new_time;
    m_time_accumulator += frame_time;
    const double dt = 1.0 / 100.0;
    //int steps = 0;
    while (m_time_accumulator >= dt)
    {
        //++steps;
        update_fixed_step(
            erhe::components::Time_context{
                .dt           = dt,
                .time         = m_time,
                .frame_number = m_frame_number
            }
        );
        m_time_accumulator -= dt;
        m_time += dt;
    }

    //log_update->trace("{} fixed update steps", steps);

    update_once_per_frame(
        erhe::components::Time_context{
            .dt           = dt,
            .time         = m_time,
            .frame_number = m_frame_number
        }
    );

    ERHE_PROFILE_FRAME_END
}

void Time::update_fixed_step(
    const erhe::components::Time_context& time_context
)
{
    ERHE_PROFILE_FUNCTION

    m_components->update_fixed_step(time_context);
}

void Time::update_once_per_frame(
    const erhe::components::Time_context& time_context
)
{
    ERHE_PROFILE_FUNCTION

    m_components->update_once_per_frame(time_context);
    ++m_frame_number;
}

auto Time::frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Time::time() const -> double
{
    return m_time;
}

}  // namespace erhe::application
