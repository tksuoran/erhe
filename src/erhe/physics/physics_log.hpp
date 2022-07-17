#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::physics
{

extern std::shared_ptr<spdlog::logger> log_physics;
extern std::shared_ptr<spdlog::logger> log_physics_frame;

void initialize_logging();

} // namespace erhe::physics
