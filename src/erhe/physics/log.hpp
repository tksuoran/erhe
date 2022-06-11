#pragma once

#include "erhe/log/log.hpp"

namespace erhe::physics
{

extern std::shared_ptr<spdlog::logger> log_physics;
extern std::shared_ptr<spdlog::logger> log_physics_frame;

void initialize_logging();

} // namespace erhe::physics
