#include "erhe_time/timestamp.hpp"

#include <fmt/format.h>

#if !defined(_POSIX_THREAD_SAFE_FUNCTIONS)
#   define _POSIX_THREAD_SAFE_FUNCTIONS 1
#endif
#include <ctime>

namespace erhe::time
{

//          111111111122
// 123456789012345678901
// 20211022 14:17:01.337
auto timestamp() -> std::string
{
    struct timespec ts{};
#if defined(_MSC_VER)
    timespec_get(&ts, TIME_UTC);
#else
    // Works at least with MinGW gcc bundled with CLion
    clock_gettime(CLOCK_REALTIME, &ts);
#endif

    struct tm time;
#if defined (_WIN32) // _MSC_VER
    localtime_s(&time, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &time);
#endif

    // Write time
    return fmt::format(
        "{:04d}{:02d}{:02d} {:02}:{:02}:{:02}.{:03d} ",
        time.tm_year + 1900,
        time.tm_mon + 1,
        time.tm_mday,
        time.tm_hour,
        time.tm_min,
        time.tm_sec,
        ts.tv_nsec / 1000000
    );
}

auto timestamp_short() -> std::string
{
    struct timespec ts{};
#if defined(_MSC_VER)
    timespec_get(&ts, TIME_UTC);
#else
    // Works at least with MinGW gcc bundled with CLion
    clock_gettime(CLOCK_REALTIME, &ts);
#endif

    struct tm time;
#if defined (_WIN32) // _MSC_VER
    localtime_s(&time, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &time);
#endif

    // Write time
    return fmt::format(
        "{:02}:{:02}:{:02}.{:03d} ",
        time.tm_hour,
        time.tm_min,
        time.tm_sec,
        ts.tv_nsec / 1000000
    );
}

}
