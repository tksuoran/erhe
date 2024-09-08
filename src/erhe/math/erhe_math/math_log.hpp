#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::math {

extern std::shared_ptr<spdlog::logger> log_input_axis;
extern std::shared_ptr<spdlog::logger> log_input_axis_frame;

void initialize_logging();

} // namespace erhe::math
