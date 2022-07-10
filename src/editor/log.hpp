#pragma once

#include "erhe/log/log.hpp"

namespace editor {

extern std::shared_ptr<spdlog::logger> log_brush;
extern std::shared_ptr<spdlog::logger> log_debug_visualization;
extern std::shared_ptr<spdlog::logger> log_draw;
extern std::shared_ptr<spdlog::logger> log_fly_camera;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_gl;
extern std::shared_ptr<spdlog::logger> log_headset;
extern std::shared_ptr<spdlog::logger> log_id_render;
extern std::shared_ptr<spdlog::logger> log_materials;
extern std::shared_ptr<spdlog::logger> log_node_properties;
extern std::shared_ptr<spdlog::logger> log_parsers;
extern std::shared_ptr<spdlog::logger> log_physics;
extern std::shared_ptr<spdlog::logger> log_pointer;
extern std::shared_ptr<spdlog::logger> log_programs;
extern std::shared_ptr<spdlog::logger> log_raytrace;
extern std::shared_ptr<spdlog::logger> log_render;
extern std::shared_ptr<spdlog::logger> log_scene;
extern std::shared_ptr<spdlog::logger> log_selection;
extern std::shared_ptr<spdlog::logger> log_svg;
extern std::shared_ptr<spdlog::logger> log_textures;
extern std::shared_ptr<spdlog::logger> log_trs_tool;
extern std::shared_ptr<spdlog::logger> log_windows;

void initialize_logging();

}
