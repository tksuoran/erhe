#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::dataformat
{

extern std::shared_ptr<spdlog::logger> log_dataformat;

void initialize_logging();

} // namespace erhe::dataformat
