#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::toolkit
{

std::shared_ptr<spdlog::logger> log_file;
std::shared_ptr<spdlog::logger> log_space_mouse;
std::shared_ptr<spdlog::logger> log_sleep;
std::shared_ptr<spdlog::logger> log_window;

void initialize_logging()
{
    log_file        = erhe::log::make_logger("erhe::toolkit::file",        spdlog::level::info);
    log_space_mouse = erhe::log::make_logger("erhe::toolkit::space_mouse", spdlog::level::info);
    log_sleep       = erhe::log::make_logger("erhe::toolkit::sleep",       spdlog::level::info);
    log_window      = erhe::log::make_logger("erhe::toolkit::window",      spdlog::level::info);
}

} // namespace erhe::toolkit
