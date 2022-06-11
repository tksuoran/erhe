#pragma once

#include "erhe/log/log.hpp"

namespace erhe::primitive
{

extern std::shared_ptr<spdlog::logger> log_primitive_builder;
extern std::shared_ptr<spdlog::logger> log_primitive;

void initialize_logging();

} // namespace erhe::primitive
