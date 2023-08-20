#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::file {

extern std::shared_ptr<spdlog::logger> log_file;

void initialize_logging();

}
