#pragma once

#include "erhe/log/log.hpp"

namespace erhe::components
{

extern std::shared_ptr<spdlog::logger> log_components;

void initialize_logging();

} // namespace erhe::components
