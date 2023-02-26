#pragma once

#if defined(ERHE_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#endif

#include <string>

namespace erhe::net
{

auto get_net_error_string(const int error_code) -> std::string;
auto is_error_fatal(int value) -> bool;

}
