#include "erhe/net/net_os.hpp"
#include "erhe/net/net_log.hpp"
#include <string.h>

#include <fmt/format.h>

namespace erhe::net
{

auto get_net_last_error() -> int
{
    return errno;
}

auto get_net_error_string(const int error_code) -> std::string
{
    const char* const description = strerror(error_code);
    return fmt::format("Error {}: {}", error_code, description);
}

auto is_error_fatal(const int error_code) -> bool
{
    if (is_error_busy(error_code)) {
        return false;
    }
    if (error_code == EISCONN) {
        return false;
    }
    return true;
}

auto is_error_busy(const int error_code) -> bool
{
    switch (error_code) {
        case EWOULDBLOCK:
        case EINPROGRESS:
        case EALREADY: {
            return true;
        }
        default: {
            return false;
        }
    }
}

auto is_socket_good(const SOCKET socket) -> bool
{
    return (socket >= 0) && (socket <= FD_SETSIZE);
}

auto set_socket_option(
    const SOCKET        socket,
    const Socket_option option,
    const int           value
) -> bool
{
    if (!is_socket_good(socket)) {
        return false;
    }
    int               result = SOCKET_ERROR;
    const void* const optval = reinterpret_cast<const void*>(&value);
    const int         optlen = sizeof(int);
    switch (option) {
        case Socket_option::Error: {
            return false;
        }
        case Socket_option::NonBlocking: {
            int flags = fcntl(socket, F_GETFL, 0);
            if (flags == -1) {
                return false;
            }
            flags = (value != 0) ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
            result = fcntl(socket, F_SETFL, flags);
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
        const int  error_code = get_net_last_error();
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
    void* const optval = reinterpret_cast<void*>(&value);
    socklen_t   optlen = sizeof(int);
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
        const int  error_code = get_net_last_error();
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

auto get_net_hints() -> addrinfo
{
    return addrinfo{
        .ai_flags     = 0,
        .ai_family    = AF_INET,
        .ai_socktype  = SOCK_STREAM,
        .ai_protocol  = IPPROTO_TCP,
        .ai_addrlen   = 0,
        .ai_addr      = nullptr,
        .ai_canonname = nullptr,
        .ai_next      = nullptr
    };
}

}
