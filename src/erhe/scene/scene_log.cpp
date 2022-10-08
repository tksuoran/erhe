#include "erhe/scene/scene_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::scene
{

std::shared_ptr<spdlog::logger> log;

void initialize_logging()
{
    log = erhe::log::make_logger("erhe::scene::log", spdlog::level::info);
}

} // namespace erhe::scene
