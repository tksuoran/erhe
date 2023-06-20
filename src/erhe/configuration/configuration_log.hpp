#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::configuration {

extern std::shared_ptr<spdlog::logger> log_configuration;

void initialize_logging();

}
