#include "erhe/net/net_os.hpp"
#include "erhe/net/net_log.hpp"

#include "fmt/format.h"

#include <cstdio>

namespace erhe::net
{

class Scoped_socket_context
{
    Scoped_socket_context();
    ~Scoped_socket_context();

    bool ok{false};
};

Scoped_socket_context::Scoped_socket_context()
{
    WSADATA wsa_data;
    const int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (res != 0) {
        log_net->error("WSAStartup failed with error: {}", res);
        return;
    }
    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
        WSACleanup();
        log_net->error("Could not find a usable version of Winsock.dll");
        return;
    }
    log_net->trace("The Winsock 2.2 dll was found okay");
    ok = true;
}

Scoped_socket_context::~Scoped_socket_context()
{
    log_net->trace("Stopping Winsock");
    WSACleanup();
}

auto initialize_net() -> bool
{
    WSADATA wsa_data;
    const int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (res != 0) {
        log_net->error("WSAStartup failed with error: {}", res);
        return false;
    }
    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
        WSACleanup();
        log_net->error("Could not find a usable version of Winsock.dll");
        return false;
    }
    log_net->trace("The Winsock 2.2 dll was found okay");
    return true;
}

auto get_net_last_error() -> int
{
    return WSAGetLastError();
}

auto get_net_error_code_string(const int error_code) -> std::string
{
    switch (error_code) {
        case WSAEINTR                         : return "WSAEINTR";
        case WSAEBADF                         : return "WSAEBADF";
        case WSAEACCES                        : return "WSAEACCES";
        case WSAEFAULT                        : return "WSAEFAULT";
        case WSAEINVAL                        : return "WSAEINVAL";
        case WSAEMFILE                        : return "WSAEMFILE";
        case WSAEWOULDBLOCK                   : return "WSAEWOULDBLOCK";
        case WSAEINPROGRESS                   : return "WSAEINPROGRESS";
        case WSAEALREADY                      : return "WSAEALREADY";
        case WSAENOTSOCK                      : return "WSAENOTSOCK";
        case WSAEDESTADDRREQ                  : return "WSAEDESTADDRREQ";
        case WSAEMSGSIZE                      : return "WSAEMSGSIZE";
        case WSAEPROTOTYPE                    : return "WSAEPROTOTYPE";
        case WSAENOPROTOOPT                   : return "WSAENOPROTOOPT";
        case WSAEPROTONOSUPPORT               : return "WSAEPROTONOSUPPORT";
        case WSAESOCKTNOSUPPORT               : return "WSAESOCKTNOSUPPORT";
        case WSAEOPNOTSUPP                    : return "WSAEOPNOTSUPP";
        case WSAEPFNOSUPPORT                  : return "WSAEPFNOSUPPORT";
        case WSAEAFNOSUPPORT                  : return "WSAEAFNOSUPPORT";
        case WSAEADDRINUSE                    : return "WSAEADDRINUSE";
        case WSAEADDRNOTAVAIL                 : return "WSAEADDRNOTAVAIL";
        case WSAENETDOWN                      : return "WSAENETDOWN";
        case WSAENETUNREACH                   : return "WSAENETUNREACH";
        case WSAENETRESET                     : return "WSAENETRESET";
        case WSAECONNABORTED                  : return "WSAECONNABORTED";
        case WSAECONNRESET                    : return "WSAECONNRESET";
        case WSAENOBUFS                       : return "WSAENOBUFS";
        case WSAEISCONN                       : return "WSAEISCONN";
        case WSAENOTCONN                      : return "WSAENOTCONN";
        case WSAESHUTDOWN                     : return "WSAESHUTDOWN";
        case WSAETOOMANYREFS                  : return "WSAETOOMANYREFS";
        case WSAETIMEDOUT                     : return "WSAETIMEDOUT";
        case WSAECONNREFUSED                  : return "WSAECONNREFUSED";
        case WSAELOOP                         : return "WSAELOOP";
        case WSAENAMETOOLONG                  : return "WSAENAMETOOLONG";
        case WSAEHOSTDOWN                     : return "WSAEHOSTDOWN";
        case WSAEHOSTUNREACH                  : return "WSAEHOSTUNREACH";
        case WSAENOTEMPTY                     : return "WSAENOTEMPTY";
        case WSAEPROCLIM                      : return "WSAEPROCLIM";
        case WSAEUSERS                        : return "WSAEUSERS";
        case WSAEDQUOT                        : return "WSAEDQUOT";
        case WSAESTALE                        : return "WSAESTALE";
        case WSAEREMOTE                       : return "WSAEREMOTE";
        case WSASYSNOTREADY                   : return "WSASYSNOTREADY";
        case WSAVERNOTSUPPORTED               : return "WSAVERNOTSUPPORTED";
        case WSANOTINITIALISED                : return "WSANOTINITIALISED";
        case WSAEDISCON                       : return "WSAEDISCON";
        case WSAENOMORE                       : return "WSAENOMORE";
        case WSAECANCELLED                    : return "WSAECANCELLED";
        case WSAEINVALIDPROCTABLE             : return "WSAEINVALIDPROCTABLE";
        case WSAEINVALIDPROVIDER              : return "WSAEINVALIDPROVIDER";
        case WSAEPROVIDERFAILEDINIT           : return "WSAEPROVIDERFAILEDINIT";
        case WSASYSCALLFAILURE                : return "WSASYSCALLFAILURE";
        case WSASERVICE_NOT_FOUND             : return "WSASERVICE_NOT_FOUND";
        case WSATYPE_NOT_FOUND                : return "WSATYPE_NOT_FOUND";
        case WSA_E_NO_MORE                    : return "WSA_E_NO_MORE";
        case WSA_E_CANCELLED                  : return "WSA_E_CANCELLED";
        case WSAEREFUSED                      : return "WSAEREFUSED";
        case WSAHOST_NOT_FOUND                : return "WSAHOST_NOT_FOUND";
        case WSATRY_AGAIN                     : return "WSATRY_AGAIN";
        case WSANO_RECOVERY                   : return "WSANO_RECOVERY";
        case WSANO_DATA                       : return "WSANO_DATA";
        default:
            return fmt::format("{}", error_code);
    }
}

auto get_net_error_message_string(const int error_code) -> std::string
{
    char* buffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buffer,
        0,
        nullptr
    );

    const std::string error_message{buffer};
    LocalFree(buffer);
    return error_message;
}

auto is_error_fatal(const int error_code) -> bool
{
    switch (error_code) {
        case WSAENETDOWN:
        case WSAENETUNREACH:
        case WSAENETRESET:
        case WSAECONNABORTED:
        case WSAECONNRESET:
        case WSAECONNREFUSED:
        case WSAEHOSTDOWN:
        case WSAEHOSTUNREACH:
        case WSAENOTCONN:
        case WSAHOST_NOT_FOUND: {
            return true;
        }
        default: {
            return false;
        }
    }
}

auto is_error_busy(const int error_code) -> bool
{
    switch (error_code) {
        case WSAEWOULDBLOCK:
        case WSAEINPROGRESS:
        case WSAEALREADY: {
            return true;
        }
        default: {
            return false;
        }
    }
}

auto is_socket_good(const SOCKET socket) -> bool
{
    return socket != INVALID_SOCKET;
}

auto set_socket_option(
    const SOCKET        socket,
    const Socket_option option,
    int                 value
) -> bool
{
    int         result = SOCKET_ERROR;
    char* const optval = reinterpret_cast<char*>(&value);
    const int   optlen = sizeof(int);
    switch (option) {
        case Socket_option::Error: {
            return false;
        }
        case Socket_option::NonBlocking: {
            u_long non_blocking = value;
            result = ioctlsocket(socket, FIONBIO, &non_blocking);
            break;
        }
        case Socket_option::ReceiveBufferSize: {
            result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, optlen);
            break;
        }
        case Socket_option::SendBufferSize: {
            result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, optval, optlen);
            break;
        }
        case Socket_option::ReuseAddress: {
            result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, optval, optlen);
            break;
        }
        case Socket_option::ReceiveTimeout: {
            result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, optval, optlen);
            break;
        }
        case Socket_option::SendTimeout: {
            result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, optlen);
            break;
        }
        case Socket_option::NoDelay: {
            result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, optval, optlen);
            break;
        }
        default: break;
    }
    if (result == SOCKET_ERROR) {
        log_net->error(
            "Setting socket option {} to value {} failed with error {}",
            c_str(option),
            value,
            get_net_last_error_message()
        );
        return false;
    }
    return true;
}

auto get_socket_option(const SOCKET socket, const Socket_option option) -> std::optional<int>
{
    int         result = SOCKET_ERROR;
    int         value  = 0;
    char* const optval = reinterpret_cast<char*>(&value);
    int         optlen = sizeof(int);
    switch (option) {
        case Socket_option::Error: {
            result = getsockopt(socket, SOL_SOCKET, SO_ERROR, optval, &optlen);
            break;
        }
        case Socket_option::ReceiveBufferSize: {
            result = getsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, &optlen);
            break;
        }
        case Socket_option::SendBufferSize: {
            result = getsockopt(socket, SOL_SOCKET, SO_SNDBUF, optval, &optlen);
            break;
        }
        default: break;
    }
    if (result == SOCKET_ERROR) {
        log_net->error(
            "Getting socket option {} failed with error {}",
            c_str(option),
            get_net_last_error_message()
        );
        return {};
    }
    return value;
}

auto get_net_hints(
    const int flags,
    const int family,
    const int socktype,
    const int protocol
) -> addrinfo
{
    return addrinfo{
        .ai_flags     = flags,
        .ai_family    = family,
        .ai_socktype  = socktype,
        .ai_protocol  = protocol,
        .ai_addrlen   = 0,
        .ai_canonname = nullptr,
        .ai_addr      = nullptr,
        .ai_next      = nullptr
    };
}

}
