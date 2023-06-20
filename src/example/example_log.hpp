#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace example {

extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging();

}
