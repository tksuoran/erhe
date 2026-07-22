#pragma once

#include <chrono>
#include <cstdint>

namespace erhe::time {

auto sleep_initialize() -> bool;
void sleep_for(std::chrono::duration<float, std::milli> time_to_sleep);
void sleep_for_100ns(int64_t time_to_sleep_in_100ns);

// High-resolution waitable timer (frame pacing steps P0.5 / P2.3). On
// Windows this is CREATE_WAITABLE_TIMER_HIGH_RESOLUTION - the only wait
// mode the P0.5 probe found usable for release gating (wake error
// p99 = 0.59 ms on the development machine vs 15.3 ms for sleep_for; the
// pacer's 1 ms guard absorbs it). Elsewhere it falls back to
// std::this_thread::sleep_for (unmeasured; revisit before pacing there).
class Waitable_timer
{
public:
    Waitable_timer();
    ~Waitable_timer() noexcept;
    Waitable_timer(const Waitable_timer&)            = delete;
    Waitable_timer& operator=(const Waitable_timer&) = delete;

    // Blocks for approximately duration_seconds; returns immediately when
    // the duration is not positive.
    void wait_for(double duration_seconds);

private:
    void* m_timer_handle{nullptr}; // HANDLE on Windows, unused elsewhere
};

} // namespace erhe::time
