#include "erhe/components/log.hpp"

namespace erhe::components
{

std::shared_ptr<spdlog::logger> log_components;

void initialize_logging()
{
    log_components = erhe::log::make_logger("erhe::components::components", spdlog::level::info);
}

} // namespace erhe::components
