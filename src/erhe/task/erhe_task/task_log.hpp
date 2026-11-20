#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::task {

extern std::shared_ptr<spdlog::logger> log_task;

void initialize_logging();

} // namespace erhe::task
