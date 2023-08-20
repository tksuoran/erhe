#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::gltf {

extern std::shared_ptr<spdlog::logger> log_gltf;

void initialize_logging();

} // namespace erhe::gltf
