#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::components
{

extern std::shared_ptr<spdlog::logger> log_components;

void initialize_logging();

} // namespace erhe::components
