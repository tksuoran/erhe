#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::scene
{

extern std::shared_ptr<spdlog::logger> log;
extern std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging();

} // namespace erhe::scene
