#pragma once

#include "erhe/log/log.hpp"

namespace erhe::raytrace
{

extern std::shared_ptr<spdlog::logger> log_buffer;
extern std::shared_ptr<spdlog::logger> log_device;
extern std::shared_ptr<spdlog::logger> log_geometry;
extern std::shared_ptr<spdlog::logger> log_scene;
extern std::shared_ptr<spdlog::logger> log_embree;

void initialize_logging();

} // namespace erhe::primitive
