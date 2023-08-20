#include "erhe_net/net_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::net
{

std::shared_ptr<spdlog::logger> log_net   ;
std::shared_ptr<spdlog::logger> log_socket;
std::shared_ptr<spdlog::logger> log_client;
std::shared_ptr<spdlog::logger> log_server;

void initialize_logging()
{
    using namespace erhe::log;
    log_net    = make_logger("erhe.net.net");
    log_socket = make_logger("erhe.net.socket");
    log_client = make_logger("erhe.net.client");
    log_server = make_logger("erhe.net.server");
}

} // namespace erhe::net
