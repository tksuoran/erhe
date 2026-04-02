#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace rendering_test {

extern std::shared_ptr<spdlog::logger> log_test;

void initialize_logging();

}
