#include "example_log.hpp"
#include "erhe/log/log.hpp"

namespace example {

std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging()
{
    log_startup = erhe::log::make_logger("example::startup", spdlog::level::info);
    log_gltf    = erhe::log::make_logger("example::gltf",    spdlog::level::info);
}

}
