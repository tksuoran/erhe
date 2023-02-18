#include "erhe/toolkit/timer.hpp"

#include <algorithm>

namespace erhe::toolkit
{

std::mutex          Timer::s_mutex;
std::vector<Timer*> Timer::s_all_timers;

Timer::Timer(const char* label)
    : m_label{label}
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_all_timers.push_back(this);
}

Timer::~Timer() noexcept
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_all_timers.erase(
        std::remove(
            s_all_timers.begin(),
            s_all_timers.end(),
            this
        ),
        s_all_timers.end()
    );
}

auto Timer::all_timers() -> std::vector<Timer*>
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    return s_all_timers;
}

auto Timer::duration() const -> std::optional<std::chrono::steady_clock::duration>
{
    if (m_start_time.has_value() && m_end_time.has_value()) {
        return m_end_time.value() - m_start_time.value();
    }
    return {};
}

auto Timer::label() const -> const char*
{
    return m_label;
}

void Timer::begin()
{
    m_start_time = std::chrono::steady_clock::now();
    m_end_time.reset();
}

void Timer::end()
{
    m_end_time = std::chrono::steady_clock::now();
}

Scoped_timer::Scoped_timer(Timer& timer)
    : m_timer{timer}
{
    timer.begin();
}

Scoped_timer::~Scoped_timer() noexcept
{
    m_timer.end();
}

//static auto all_timers() -> std::vector<Timer*>;

}
