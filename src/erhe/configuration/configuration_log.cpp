#include "erhe/configuration/configuration_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::configuration {

std::shared_ptr<spdlog::logger> log_configuration;

void initialize_logging()
{
    using namespace erhe::log;
    log_configuration = make_logger("erhe.configuration");
}

}
