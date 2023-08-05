#include "example_log.hpp"
#include "erhe/log/log.hpp"

namespace example {

std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging()
{
    using namespace erhe::log;
    log_startup = make_logger("example.startup");
    log_gltf    = make_logger("example.gltf"   );
}

}
