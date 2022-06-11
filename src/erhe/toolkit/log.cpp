#include "erhe/toolkit/log.hpp"

namespace erhe::toolkit
{

std::shared_ptr<spdlog::logger> log_file;
std::shared_ptr<spdlog::logger> log_space_mouse;

void initialize_logging()
{
    log_file        = erhe::log::make_logger("erhe::toolkit::file",        spdlog::level::info);
    log_space_mouse = erhe::log::make_logger("erhe::toolkit::space_mouse", spdlog::level::info);
}

} // namespace erhe::toolkit
