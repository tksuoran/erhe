#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::usd {

extern std::shared_ptr<spdlog::logger> log_usd;

void initialize_logging();

}
