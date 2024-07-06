#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::time {

extern std::shared_ptr<spdlog::logger> log_time;

void initialize_logging();

} // namespace erhe::time
