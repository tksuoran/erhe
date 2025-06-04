#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace gl {

extern std::shared_ptr<spdlog::logger> log_gl;

void initialize_logging();

} // namespace gl

