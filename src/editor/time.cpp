#include "time.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_graphics/gpu_timer.hpp"

namespace editor {

void Time::prepare_update()
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    int64_t host_system_frame_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t host_system_frame_duration_ns   = host_system_frame_start_time_ns - m_host_system_last_frame_start_time;
    int64_t simulation_frame_duration_ns    = host_system_frame_duration_ns;

    m_host_system_last_frame_duration_ns = host_system_frame_duration_ns;

    // Cap frame duration to 25ms. This causes time dilation
    if (simulation_frame_duration_ns > 25'000'000'000) {
        simulation_frame_duration_ns = 25'000'000'000;
    }

    //m_current_time = new_time;
    m_simulation_time_accumulator += simulation_frame_duration_ns;
    m_simulation_dt_ns = 1'000'000'000 / 240;
    const float simulation_dt_s = 1.0f / 240.0f;
    m_this_frame_fixed_steps.clear();
    uint64_t subframe = 0;
    while (m_simulation_time_accumulator >= m_simulation_dt_ns) {
        m_this_frame_fixed_steps.push_back(
            Time_context{
                .simulation_dt_s     = simulation_dt_s,
                .simulation_dt_ns    = m_simulation_dt_ns,
                .simulation_time_ns  = m_simulation_time_ns,
                .frame_number        = m_frame_number,
                .subframe            = subframe
            }
        );
        m_simulation_time_accumulator -= m_simulation_dt_ns;
        m_simulation_time_ns += m_simulation_dt_ns;
        ++subframe;
    }
    m_host_system_time_ns = m_host_system_last_frame_start_time;
    if (subframe > 0) {
        int64_t host_system_dt_ns = host_system_frame_duration_ns / subframe;
        double  host_system_dt_s_ = static_cast<double>(host_system_frame_duration_ns) / static_cast<double>(subframe * 1'000'000'000.0);
        float   host_system_dt_s  = static_cast<float>(host_system_dt_s_);
        for (Time_context& time_context : m_this_frame_fixed_steps) {
            time_context.host_system_dt_s    = host_system_dt_s;
            time_context.host_system_dt_ns   = host_system_dt_ns,
            time_context.host_system_time_ns = m_host_system_time_ns;
            m_host_system_time_ns += host_system_dt_ns;
        }
    }

    m_host_system_last_frame_start_time = host_system_frame_start_time_ns;
}

void Time::for_each_fixed_step(std::function<void(const Time_context&)> callback)
{
    for (const Time_context& time_context : m_this_frame_fixed_steps) {
        callback(time_context);
    }
}

auto Time::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Time::get_simulation_time_ns() const -> int64_t
{
    return m_simulation_time_ns;
}

auto Time::get_host_system_time_ns() const -> int64_t
{
    return m_host_system_time_ns;
}

auto Time::get_host_system_last_frame_duration_ns() const -> int64_t
{
    return m_host_system_last_frame_duration_ns;
}

}  // namespace editor

