#pragma once

#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <string>

namespace erhe::time {

class Timer
{
public:
    explicit Timer(const char* label);
    ~Timer() noexcept;

    Timer         (const Timer&) = delete;
    auto operator=(const Timer&) = delete;
    Timer         (Timer&&)      = delete;
    auto operator=(Timer&&)      = delete;

    [[nodiscard]] auto duration       () const -> std::optional<std::chrono::steady_clock::duration>;
    [[nodiscard]] auto label          () const -> const char*;
    [[nodiscard]] auto format_duration() const -> std::string;
    void begin();
    void end  ();

    static auto all_timers() -> std::vector<Timer*>;

private:
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Timer*>                        s_all_timers;

    std::optional<std::chrono::steady_clock::time_point> m_start_time;
    std::optional<std::chrono::steady_clock::time_point> m_end_time;
    const char*                                          m_label{nullptr};
};

class Scoped_timer
{
public:
    explicit Scoped_timer(Timer& timer);
    ~Scoped_timer() noexcept;

private:
    Timer& m_timer;
};

[[nodiscard]] auto format_duration(std::chrono::steady_clock::duration duration) -> std::string;

} // namespace erhe::time
