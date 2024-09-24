#include "time.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_graphics/gpu_timer.hpp"

namespace editor {

Update_time_base::Update_time_base(Time& time)
    : m_time{time}
{
}

Update_time_base::~Update_time_base() noexcept = default;

Update_fixed_step::Update_fixed_step()
{
    m_time.register_update_fixed_step(this);
}

Update_fixed_step::~Update_fixed_step() noexcept
{
    m_time.unregister_update_fixed_step(this);
}

Update_once_per_frame::Update_once_per_frame()
{
    m_time.register_update_once_per_frame(this);
}

Update_once_per_frame::~Update_once_per_frame() noexcept
{
    m_time.unregister_update_once_per_frame(this);
}

void Time::register_update_fixed_step(Update_fixed_step* entry)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_update_fixed_step.push_back(entry);
}

void Time::register_update_once_per_frame(Update_once_per_frame* entry)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_update_once_per_frame.push_back(entry);
}

void Time::unregister_update_fixed_step(Update_fixed_step* entry)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_update_fixed_step.erase(std::remove(m_update_fixed_step.begin(), m_update_fixed_step.end(), entry), m_update_fixed_step.end());
}

void Time::unregister_update_once_per_frame(Update_once_per_frame* entry)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_update_once_per_frame.erase(std::remove(m_update_once_per_frame.begin(), m_update_once_per_frame.end(), entry), m_update_once_per_frame.end());
}

void Time::start_time()
{
    m_current_time = std::chrono::steady_clock::now();
}

void Time::update()
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto new_time   = std::chrono::steady_clock::now();
    const auto duration   = new_time - m_current_time;
    double     frame_time = std::chrono::duration<double, std::ratio<1>>(duration).count();

    if (frame_time > 0.25) {
        frame_time = 0.25;
    }

    m_current_time = new_time;
    m_time_accumulator += frame_time;
    const double dt = 1.0 / 240.0;
    //int steps = 0;
    while (m_time_accumulator >= dt) {
        //++steps;
        update_fixed_step(
            Time_context{
                // TODO .timestamp    = new_time,
                .dt           = dt,
                .time         = m_time,
                .frame_number = m_frame_number
            }
        );
        m_time_accumulator -= dt;
        m_time += dt;
    }

    //log_update->trace("{} fixed update steps", steps);

    // For once per frame
    m_last_update = Time_context{
        .timestamp    = new_time, 
        .dt           = dt,
        .time         = m_time,
        .frame_number = m_frame_number
    };
    update_once_per_frame();

    //ERHE_PROFILE_FRAME_END
}

void Time::update_fixed_step(const Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION();

    for (auto* update : m_update_fixed_step) {
        update->update_fixed_step(time_context);
    }
}

void Time::update_once_per_frame()
{
    ERHE_PROFILE_FUNCTION();

    for (auto* update : m_update_once_per_frame) {
        update->update_once_per_frame(m_last_update);
    }
    erhe::graphics::Gpu_timer::end_frame();
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

}  // namespace editor

