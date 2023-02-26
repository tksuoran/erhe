#include "erhe/net/select_sockets.hpp"

namespace erhe::net
{

Select_sockets::Select_sockets()
{
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
}

auto Select_sockets::has_read() const -> bool
{
    return (flags & flag_read) == flag_read;
}

auto Select_sockets::has_write() const -> bool
{
    return (flags & flag_write) == flag_write;
}

auto Select_sockets::has_except() const -> bool
{
    return (flags & flag_except) == flag_except;
}

auto Select_sockets::has_read(const SOCKET socket) const -> bool
{
    return FD_ISSET(socket, &read_fds) == TRUE;
}

auto Select_sockets::has_write(const SOCKET socket) const -> bool
{
    return FD_ISSET(socket, &write_fds) == TRUE;
}

auto Select_sockets::has_except(const SOCKET socket) const -> bool
{
    return FD_ISSET(socket, &except_fds) == TRUE;
}

void Select_sockets::set_read(const SOCKET socket)
{
    FD_SET(socket, &read_fds); flags = flags | flag_read;
}

void Select_sockets::set_write(const SOCKET socket)
{
    FD_SET(socket, &write_fds ); flags = flags | flag_write;
}

void Select_sockets::set_except(const SOCKET socket)
{
    FD_SET(socket, &except_fds);
    flags = flags | flag_except;
}

auto Select_sockets::select(const int timeout_ms) -> int
{
    timeval timeout{
        .tv_sec  = 0,
        .tv_usec = timeout_ms * 1000
    };
    return ::select(
        0,
        has_read  () ? &read_fds   : nullptr,
        has_write () ? &write_fds  : nullptr,
        has_except() ? &except_fds : nullptr,
        &timeout
    );
}

}
