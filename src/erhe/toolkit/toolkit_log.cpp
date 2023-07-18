#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::toolkit
{

std::shared_ptr<spdlog::logger> log_file;
std::shared_ptr<spdlog::logger> log_space_mouse;
std::shared_ptr<spdlog::logger> log_sleep;
std::shared_ptr<spdlog::logger> log_window;
std::shared_ptr<spdlog::logger> log_window_event;
std::shared_ptr<spdlog::logger> log_renderdoc;

void initialize_logging()
{
    log_file         = erhe::log::make_logger("toolkit::file",         spdlog::level::info);
    log_space_mouse  = erhe::log::make_logger("toolkit::space_mouse",  spdlog::level::info);
    log_sleep        = erhe::log::make_logger("toolkit::sleep",        spdlog::level::info);
    log_window       = erhe::log::make_logger("toolkit::window",       spdlog::level::info);
    log_window_event = erhe::log::make_logger("toolkit::window_event", spdlog::level::info);
    log_renderdoc    = erhe::log::make_logger("toolkit::renderdoc",    spdlog::level::info);
}

} // namespace erhe::toolkit
