#pragma once

#if defined(ERHE_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
using socklen_t = int;
#endif

#if defined(ERHE_OS_LINUX)
#   include <arpa/inet.h>
#   include <errno.h>
#   include <fcntl.h>
#   include <netdb.h>
#   include <sys/select.h>
#   include <sys/socket.h>
#   include <sys/types.h>
#   include <unistd.h>

// For now, pretent Windows like API... TODO fix
using SOCKET = int;
using FD_SET = fd_set;
using PCSTR  = const char*;
static constexpr int    TRUE           = 1;
static constexpr SOCKET INVALID_SOCKET = -1;
static constexpr int    SOCKET_ERROR   = -1;
static constexpr int    WSAEISCONN     = EISCONN;
static constexpr int    WSAEWOULDBLOCK = EINPROGRESS; // TODO 
static constexpr int    SD_BOTH        = SHUT_RDWR;
inline auto closesocket(const SOCKET s) -> int { return close(s); }
inline auto WSAGetLastError() -> int { return errno; }
#endif

#include <string>

namespace erhe::net
{

auto get_net_error_string(const int error_code) -> std::string;
auto is_error_fatal(int value) -> bool;


inline bool set_socket_blocking_enabled(int fd, bool blocking)
{
#ifdef _WIN32
    unsigned long mode = blocking ? 0 : 1;
    return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? true : false;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
#endif
}

}
