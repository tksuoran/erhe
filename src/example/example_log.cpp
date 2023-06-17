#include "example_log.hpp"
#include "erhe/log/log.hpp"

namespace example {

std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging()
{
    log_gltf = erhe::log::make_logger("gltf", spdlog::level::info);
}

}
