#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::primitive
{

extern std::shared_ptr<spdlog::logger> log_primitive_builder;
extern std::shared_ptr<spdlog::logger> log_primitive;

void initialize_logging();

} // namespace erhe::primitive
