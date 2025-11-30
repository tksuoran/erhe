#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace hello_swap {

extern std::shared_ptr<spdlog::logger> log_startup;

void initialize_logging();

}
