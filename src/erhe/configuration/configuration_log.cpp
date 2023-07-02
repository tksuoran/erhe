#include "erhe/configuration/configuration_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::configuration {

std::shared_ptr<spdlog::logger> log_configuration;

void initialize_logging()
{
    log_configuration = erhe::log::make_logger("configuration", spdlog::level::info);
}

}
