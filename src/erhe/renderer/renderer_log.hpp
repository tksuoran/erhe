#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::renderer {

extern std::shared_ptr<spdlog::logger> log_draw;
extern std::shared_ptr<spdlog::logger> log_render;
extern std::shared_ptr<spdlog::logger> log_program_interface;

void initialize_logging();

}
