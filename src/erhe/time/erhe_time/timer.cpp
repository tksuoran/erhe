#include "erhe_time/timer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace erhe::time {

ERHE_PROFILE_MUTEX(std::mutex, Timer::s_mutex);
std::vector<Timer*>            Timer::s_all_timers;

Timer::Timer(const char* label)
    : m_label{label}
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_all_timers.push_back(this);
}

Timer::~Timer() noexcept
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

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
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

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

auto Timer::format_duration() const -> std::string
{
    if (m_start_time.has_value() && m_end_time.has_value()) {
        return erhe::time::format_duration(m_end_time.value() - m_start_time.value());
    }
    return {};
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

auto format_duration(std::chrono::steady_clock::duration duration) -> std::string
{
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(duration);
    auto s = duration_cast<seconds>(ms);
    ms -= duration_cast<milliseconds>(s);
    auto m = duration_cast<minutes>(s);
    s -= duration_cast<seconds>(m);
    auto h = duration_cast<hours>(m);
    m -= duration_cast<minutes>(h);

    const int hours = static_cast<int>(h.count());
    const int minutes = static_cast<int>(m.count());
    const int seconds = static_cast<int>(s.count());
    const int milliseconds = static_cast<int>(ms.count());
    if (hours > 0) {
        return fmt::format("{}:{:02}:{:02}.{:03}", hours, minutes, seconds, milliseconds);
    } else if (minutes) {
        return fmt::format("{}:{:02}.{:03}", minutes, seconds, milliseconds);
    } else {
        return fmt::format("{}.{:03}", seconds, milliseconds);
    }
}

//static auto all_timers() -> std::vector<Timer*>;

} // namespace erhe::time
