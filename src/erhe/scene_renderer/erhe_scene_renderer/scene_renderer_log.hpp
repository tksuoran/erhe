#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::scene_renderer {

extern std::shared_ptr<spdlog::logger> log_draw;
extern std::shared_ptr<spdlog::logger> log_render;
extern std::shared_ptr<spdlog::logger> log_program_interface;
extern std::shared_ptr<spdlog::logger> log_shadow_renderer;
extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_buffer_writer;
extern std::shared_ptr<spdlog::logger> log_multi_buffer;

void initialize_logging();

} // namespace erhe::scene_renderer
