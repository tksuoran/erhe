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

}
