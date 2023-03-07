#include "erhe/net/net_os.hpp"
#include "erhe/net/net_log.hpp"

#include <cstdio>

namespace erhe::net
{

auto c_str(const Socket_option option) -> const char*
{
    switch (option) {
        case Socket_option::Error            : return "Error";
        case Socket_option::NonBlocking      : return "NonBlocking";
        case Socket_option::ReceiveBufferSize: return "ReceivevBufferSize";
        case Socket_option::SendBufferSize   : return "SendBufferSize";
        case Socket_option::ReuseAddress     : return "ReuseAddress";
        case Socket_option::ReceiveTimeout   : return "ReceiveTimeout";
        case Socket_option::SendTimeout      : return "SendTimeout";
        case Socket_option::NoDelay          : return "NoDelay";
        default:                               return "?";
    }
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
