#include "erhe_time/sleep.hpp"
#include "erhe_time/time_log.hpp"

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

}
