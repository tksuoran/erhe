#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::graphics {

extern std::shared_ptr<spdlog::logger> log_buffer;
extern std::shared_ptr<spdlog::logger> log_context;
extern std::shared_ptr<spdlog::logger> log_debug;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_fragment_outputs;
extern std::shared_ptr<spdlog::logger> log_glsl;
extern std::shared_ptr<spdlog::logger> log_load_png;
extern std::shared_ptr<spdlog::logger> log_program;
extern std::shared_ptr<spdlog::logger> log_save_png;
extern std::shared_ptr<spdlog::logger> log_shader_monitor;
extern std::shared_ptr<spdlog::logger> log_texture;
extern std::shared_ptr<spdlog::logger> log_texture_frame;
extern std::shared_ptr<spdlog::logger> log_threads;
extern std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
extern std::shared_ptr<spdlog::logger> log_vertex_stream;
extern std::shared_ptr<spdlog::logger> log_render_pass;
extern std::shared_ptr<spdlog::logger> log_startup;

void initialize_logging();

} // namespace erhe::graphics
