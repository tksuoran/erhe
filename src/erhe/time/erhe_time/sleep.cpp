#include "erhe_time/sleep.hpp"
#include "erhe_time/time_log.hpp"
#include "erhe_profile/profile.hpp"

#if defined(_WIN32)
#   include <Windows.h>
#else
#   include <time.h>
#endif

#include <thread>

namespace erhe::time {

#if defined(_WIN32)
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#ifndef NT_SUCCESS
#   define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace {

bool is_initialized = false;
NTSTATUS(__stdcall *NtDelayExecution    )(BOOL Alertable, PLARGE_INTEGER DelayInterval) = nullptr;
NTSTATUS(__stdcall *ZwSetTimerResolution)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution) = nullptr;
std::chrono::duration<long long, std::ratio<1, 10000000>> resolution;

}

#endif


#if defined(_WIN32)
auto sleep_initialize() -> bool
{
    ERHE_PROFILE_FUNCTION();

    HMODULE ntdll_module = GetModuleHandleA("ntdll.dll");
    if (ntdll_module == nullptr) {
        log_time->warn("Could not open ntdll.dll");
        return false;
    }

    NtDelayExecution = (NTSTATUS(__stdcall*)(BOOL, PLARGE_INTEGER)  ) GetProcAddress(ntdll_module, "NtDelayExecution");
    if (NtDelayExecution == nullptr) {
        log_time->warn("NtDelayExecution() not found in ntdll.dll");
        return false;
    }

    ZwSetTimerResolution = (NTSTATUS(__stdcall*)(ULONG, BOOLEAN, PULONG)) GetProcAddress(ntdll_module, "ZwSetTimerResolution");
    if (ZwSetTimerResolution == nullptr) {
        log_time->warn("ZwSetTimerResolution() not found in ntdll.dll");
        return false;
    }

    ULONG resolution_in_hundred_nanoseconds{};
    NTSTATUS status = ZwSetTimerResolution(1, true, &resolution_in_hundred_nanoseconds);
    if (!NT_SUCCESS(status)) {
        log_time->warn("ZwSetTimerResolution() failed.");
        return false;
    }

    resolution = std::chrono::duration<long long, std::ratio<1, 10000000>>{resolution_in_hundred_nanoseconds};

    is_initialized = true;

    return true;
}

void sleep_for(std::chrono::duration<float, std::milli> time_to_sleep)
{
    //const auto now      = std::chrono::system_clock::now();
    //const auto end_time = now + time_to_sleep;

    if (is_initialized) {
        LARGE_INTEGER duration;
        duration.QuadPart = -1 * static_cast<int>(time_to_sleep.count() * 10000.0f);
        NtDelayExecution(FALSE, &duration);
    } else {
        int int_count = static_cast<int>(time_to_sleep.count());
        Sleep(int_count);
    }
}

void sleep_for_100ns(int64_t time_to_sleep_in_100ns)
{
    if (is_initialized) {
        LARGE_INTEGER duration;
        duration.QuadPart = -time_to_sleep_in_100ns;
        NtDelayExecution(FALSE, &duration);
    }
}

#else

auto sleep_initialize() -> bool
{
    return true;
}

void sleep_for(std::chrono::duration<float, std::milli> time_to_sleep)
{
    std::this_thread::sleep_for(time_to_sleep);
}

void sleep_for_100ns(int64_t time_to_sleep_in_100ns)
{
    struct timespec ts;
    ts.tv_sec  =  time_to_sleep_in_100ns / 10000000;
    ts.tv_nsec = (time_to_sleep_in_100ns % 10000000) * 100;
    while (
        (nanosleep(&ts, &ts) == -1) &&
        (errno == EINTR)
    ) {
        log_time->warn("nanosleep() EINTR");
    }
}

#endif

#if defined(_WIN32)

Waitable_timer::Waitable_timer()
{
    // Falls back to a regular waitable timer when the high-resolution kind
    // is unavailable (pre-1803 Windows 10); wait_for() falls back to
    // sleep_for() when neither can be created.
    m_timer_handle = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );
    if (m_timer_handle == nullptr) {
        m_timer_handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_MANUAL_RESET, TIMER_ALL_ACCESS);
    }
}

Waitable_timer::~Waitable_timer() noexcept
{
    if (m_timer_handle != nullptr) {
        CloseHandle(m_timer_handle);
    }
}

void Waitable_timer::wait_for(const double duration_seconds)
{
    if (!(duration_seconds > 0.0)) {
        return;
    }
    if (m_timer_handle == nullptr) {
        sleep_for(std::chrono::duration<float, std::milli>{static_cast<float>(duration_seconds * 1000.0)});
        return;
    }
    LARGE_INTEGER due_time;
    due_time.QuadPart = -static_cast<LONGLONG>(duration_seconds * 1e7); // negative = relative, 100 ns units
    if (SetWaitableTimer(m_timer_handle, &due_time, 0, nullptr, nullptr, FALSE) == 0) {
        sleep_for(std::chrono::duration<float, std::milli>{static_cast<float>(duration_seconds * 1000.0)});
        return;
    }
    WaitForSingleObject(m_timer_handle, INFINITE);
}

#else

Waitable_timer::Waitable_timer() = default;

Waitable_timer::~Waitable_timer() noexcept = default;

void Waitable_timer::wait_for(const double duration_seconds)
{
    if (!(duration_seconds > 0.0)) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::duration<double>{duration_seconds});
}

#endif

}
