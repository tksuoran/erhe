#include "hello_swap_log.hpp"
#include "erhe_log/log.hpp"

namespace hello_swap {

std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_swap;

void initialize_logging()
{
    using namespace erhe::log;
    log_startup = make_logger("hello_swap.startup");
    log_swap    = make_logger("hello_swap.swap");
}

}
