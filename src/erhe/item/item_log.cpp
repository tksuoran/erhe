#include "erhe/scene/scene_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::item
{

std::shared_ptr<spdlog::logger> log;
std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging()
{
    log       = erhe::log::make_logger("erhe::item::log",       spdlog::level::info);
    log_frame = erhe::log::make_logger("erhe::item::log_frame", spdlog::level::info, false);
}

} // namespace erhe::item
