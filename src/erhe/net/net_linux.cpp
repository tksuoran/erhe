#include "erhe/net/net_os.hpp"

namespace erhe::net
{

auto get_net_error_string(const int error_code) -> std::string
{
    static_cast<void>(error_code);
    return "TOOD";
}

auto is_error_fatal(const int error_code) -> bool
{
    static_cast<void>(error_code);
    return false; // TODO
}

}
