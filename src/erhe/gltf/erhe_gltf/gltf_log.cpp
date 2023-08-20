#include "erhe_gltf/gltf_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::gltf {

std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging()
{
    log_gltf = erhe::log::make_logger("erhe.gltf.log");
}

} // namespace erhe::gltf
