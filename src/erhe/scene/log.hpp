#pragma once

#include "erhe/log/log.hpp"

namespace erhe::scene
{

extern std::shared_ptr<spdlog::logger> log;

void initialize_logging();

} // namespace erhe::scene
