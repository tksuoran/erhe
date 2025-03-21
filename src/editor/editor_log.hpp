#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace editor {

extern std::shared_ptr<spdlog::logger> log_asset_browser;
extern std::shared_ptr<spdlog::logger> log_brush;
extern std::shared_ptr<spdlog::logger> log_composer;
extern std::shared_ptr<spdlog::logger> log_controller_ray;
extern std::shared_ptr<spdlog::logger> log_debug_visualization;
extern std::shared_ptr<spdlog::logger> log_draw;
extern std::shared_ptr<spdlog::logger> log_fly_camera;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_headset;
extern std::shared_ptr<spdlog::logger> log_hud;
extern std::shared_ptr<spdlog::logger> log_id_render;
extern std::shared_ptr<spdlog::logger> log_input;
extern std::shared_ptr<spdlog::logger> log_input_frame;
extern std::shared_ptr<spdlog::logger> log_materials;
extern std::shared_ptr<spdlog::logger> log_net;
extern std::shared_ptr<spdlog::logger> log_node_properties;
extern std::shared_ptr<spdlog::logger> log_operations;
extern std::shared_ptr<spdlog::logger> log_parsers;
extern std::shared_ptr<spdlog::logger> log_physics;
extern std::shared_ptr<spdlog::logger> log_physics_frame;
extern std::shared_ptr<spdlog::logger> log_pointer;
extern std::shared_ptr<spdlog::logger> log_post_processing;
extern std::shared_ptr<spdlog::logger> log_programs;
extern std::shared_ptr<spdlog::logger> log_raytrace;
extern std::shared_ptr<spdlog::logger> log_raytrace_frame;
extern std::shared_ptr<spdlog::logger> log_render;
extern std::shared_ptr<spdlog::logger> log_rendertarget_imgui_windows;
extern std::shared_ptr<spdlog::logger> log_scene;
extern std::shared_ptr<spdlog::logger> log_scene_view;
extern std::shared_ptr<spdlog::logger> log_selection;
extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_svg;
extern std::shared_ptr<spdlog::logger> log_textures;
extern std::shared_ptr<spdlog::logger> log_tools;
extern std::shared_ptr<spdlog::logger> log_tree;
extern std::shared_ptr<spdlog::logger> log_tree_frame;
extern std::shared_ptr<spdlog::logger> log_trs_tool;
extern std::shared_ptr<spdlog::logger> log_trs_tool_frame;
extern std::shared_ptr<spdlog::logger> log_xr;
extern std::shared_ptr<spdlog::logger> log_graph_editor;

void initialize_logging();

}
