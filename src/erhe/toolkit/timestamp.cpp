#include "erhe/toolkit/timestamp.hpp"

#include <fmt/format.h>

#include <ctime>

namespace erhe::toolkit
{

//          111111111122
// 123456789012345678901
// 20211022 14:17:01.337
auto timestamp() -> std::string
{
    struct timespec ts{};
    timespec_get(&ts, TIME_UTC);

    struct tm time;
#ifdef _MSC_VER
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

} // namespace erhe::toolkit
