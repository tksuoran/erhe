#include "erhe/net/net_os.hpp"


auto get_net_error_string(const int error_code) -> std::string
{
}

auto is_error_fatal(int value) -> bool
{
    switch (value) {
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
