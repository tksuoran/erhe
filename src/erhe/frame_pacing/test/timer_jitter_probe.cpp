// Timer wake-up jitter probe (implementation plan step P0.5).
//
// Measures the wake-up error of the highest-resolution wait available on
// this machine, to ground the frame pacer 'guard' tunable
// (doc/frame_pacing_inputs.md section 3.4, gap G6). Standalone console
// tool, no erhe dependencies; run it on the target machine and read the
// p99 line - that is the evidence-based 'guard' floor.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto to_ms(const Clock::duration duration) -> double
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

void report(const char* label, std::vector<double>& errors_ms)
{
    std::sort(errors_ms.begin(), errors_ms.end());
    const std::size_t n = errors_ms.size();
    const auto at = [&](const double q) -> double {
        const std::size_t index = std::min(n - 1, static_cast<std::size_t>(q * static_cast<double>(n)));
        return errors_ms[index];
    };
    std::printf(
        "%-28s samples=%zu  min=%8.4f ms  median=%8.4f ms  p95=%8.4f ms  p99=%8.4f ms  max=%8.4f ms\n",
        label, n, errors_ms.front(), at(0.5), at(0.95), at(0.99), errors_ms.back()
    );
}

// One wait of target_delay; returns wake-up error (actual - requested), never negative in theory.
template <typename Wait>
void measure(const char* label, const Wait& wait, const int samples)
{
    std::mt19937 engine{12345};
    std::uniform_real_distribution<double> delay_ms{1.0, 5.0};
    std::vector<double> errors;
    errors.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const auto target_delay = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::milli>{delay_ms(engine)}
        );
        const Clock::time_point start  = Clock::now();
        const Clock::time_point target = start + target_delay;
        wait(target_delay);
        const Clock::time_point end = Clock::now();
        errors.push_back(to_ms(end - target));
    }
    report(label, errors);
}

} // anonymous namespace

int main()
{
    constexpr int samples = 500;
    std::printf("Timer wake-up jitter probe (%d samples per mode, 1..5 ms waits)\n", samples);
    std::printf("Wake error = actual wake time - requested wake time.\n\n");

#if defined(_WIN32)
    {
        HANDLE timer = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS
        );
        if (timer != nullptr) {
            measure(
                "waitable timer (high-res)",
                [timer](const Clock::duration delay) {
                    LARGE_INTEGER due;
                    const auto hundreds_of_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count() / 100;
                    due.QuadPart = -static_cast<LONGLONG>(hundreds_of_ns);
                    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(timer, INFINITE);
                },
                samples
            );
            CloseHandle(timer);
        } else {
            std::printf("high-resolution waitable timer not available on this system\n");
        }
    }
#endif

    measure(
        "std::this_thread::sleep_for",
        [](const Clock::duration delay) {
            std::this_thread::sleep_for(delay);
        },
        samples
    );

    std::printf(
        "\nRecommendation: frame pacer 'guard' should be at least the p99 of the\n"
        "wait mode the integration uses (doc/frame_pacing_inputs.md section 3.5).\n"
    );
    return 0;
}
