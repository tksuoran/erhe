#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::xr {

extern std::shared_ptr<spdlog::logger> log_xr;

void initialize_logging();

} // namespace erhe::xr
