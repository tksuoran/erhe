#pragma once

#include "erhe/log/log.hpp"

namespace erhe::graphics
{

extern std::shared_ptr<spdlog::logger> log_buffer;
extern std::shared_ptr<spdlog::logger> log_configuration;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_fragment_outputs;
extern std::shared_ptr<spdlog::logger> log_glsl;
extern std::shared_ptr<spdlog::logger> log_load_png;
extern std::shared_ptr<spdlog::logger> log_program;
extern std::shared_ptr<spdlog::logger> log_save_png;
extern std::shared_ptr<spdlog::logger> log_texture;
extern std::shared_ptr<spdlog::logger> log_threads;
extern std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
extern std::shared_ptr<spdlog::logger> log_vertex_stream;

void initialize_logging();

} // namespace erhe::graphics
