#include "erhe/net/net_os.hpp"
#include "erhe/net/net_log.hpp"

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

auto get_net_last_error() -> int
{
    return WSAGetLastError();
}

auto get_net_error_string(const int error_code) -> std::string
{
    char* buffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
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
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error(
            "Setting socket option {} to value {} failed with error {} - {}",
            c_str(option),
            value,
            error_code,
            message
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
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error(
            "Getting socket option {} failed with error {} - {}",
            c_str(option),
            error_code,
            message
        );
        return {};
    }
    return value;
}

}
