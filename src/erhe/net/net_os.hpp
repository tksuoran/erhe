#pragma once

#if defined(ERHE_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
using socklen_t = int;

static constexpr int ERHE_NET_ERROR_CODE_CONNECTED = WSAEISCONN;
#endif

#if defined(ERHE_OS_LINUX)
#   include <arpa/inet.h>
#   include <errno.h>
#   include <fcntl.h>
#   include <netdb.h>
#   include <netinet/tcp.h>
#   include <sys/select.h>
#   include <sys/socket.h>
#   include <sys/types.h>
#   include <unistd.h>

// For now, pretent Windows like API... TODO fix
using SOCKET = int;          // windows uses unsigned
using FD_SET = fd_set;
using PCSTR  = const char*;
static constexpr int    ERHE_NET_ERROR_CODE_CONNECTED = EISCONN;
static constexpr int    TRUE           = 1;
static constexpr SOCKET INVALID_SOCKET = -1;
static constexpr int    SOCKET_ERROR   = -1;
static constexpr int    SD_BOTH        = SHUT_RDWR;
inline auto closesocket(const SOCKET s) -> int { return close(s); }
#endif

#include <optional>
#include <string>

namespace erhe::net
{

auto get_net_last_error          () -> int;
auto get_net_error_code_string   (const int error_code) -> std::string;
auto get_net_error_message_string(const int error_code) -> std::string;
auto get_net_error_message       (const int error_code) -> std::string;
auto get_net_last_error_message  () -> std::string;
auto get_net_hints               (int flags, int family, int socktype, int protocol) -> addrinfo;
auto is_error_fatal              (int error_code) -> bool;
auto is_error_busy               (int error_code) -> bool;
auto is_socket_good              (SOCKET socket) -> bool;

enum class Socket_option : unsigned int
{
    Error             = 0,
    NonBlocking       = 1,
    ReceiveBufferSize = 2,
    SendBufferSize    = 3,
    ReuseAddress      = 4,
    ReceiveTimeout    = 5,
    SendTimeout       = 6,
    NoDelay           = 8
};

auto c_str(Socket_option) -> const char*;
auto set_socket_option(SOCKET socket, Socket_option option, int value) -> bool;
auto get_socket_option(SOCKET socket, Socket_option option) -> std::optional<int>;

auto initialize_net() -> bool;

}
