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

auto get_net_error_message(const int error_code) -> std::string
{
    return fmt::format(
        "{} - {}",
        get_net_error_code_string   (error_code),
        get_net_error_message_string(error_code)
    );
}

auto get_net_last_error_message() -> std::string
{
    const auto error_code = get_net_last_error();
    return get_net_error_message(error_code);
}

}
