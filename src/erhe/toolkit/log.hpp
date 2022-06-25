#pragma once

#include "erhe/log/log.hpp"

namespace erhe::toolkit
{

extern std::shared_ptr<spdlog::logger> log_file;
extern std::shared_ptr<spdlog::logger> log_space_mouse;
extern std::shared_ptr<spdlog::logger> log_sleep;
extern std::shared_ptr<spdlog::logger> log_window;

void initialize_logging();

} // namespace erhe::geometry
