#include "editor_log.hpp"
#include "erhe_log/log.hpp"

namespace editor {

std::shared_ptr<spdlog::logger> log_asset_browser;
std::shared_ptr<spdlog::logger> log_brush;
std::shared_ptr<spdlog::logger> log_composer;
std::shared_ptr<spdlog::logger> log_controller_ray;
std::shared_ptr<spdlog::logger> log_debug_visualization;
std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_fly_camera;
std::shared_ptr<spdlog::logger> log_framebuffer;
std::shared_ptr<spdlog::logger> log_headset;
std::shared_ptr<spdlog::logger> log_hud;
std::shared_ptr<spdlog::logger> log_id_render;
std::shared_ptr<spdlog::logger> log_input;
std::shared_ptr<spdlog::logger> log_input_frame;
std::shared_ptr<spdlog::logger> log_materials;
std::shared_ptr<spdlog::logger> log_net;
std::shared_ptr<spdlog::logger> log_node_properties;
std::shared_ptr<spdlog::logger> log_operations;
std::shared_ptr<spdlog::logger> log_parsers;
std::shared_ptr<spdlog::logger> log_physics;
std::shared_ptr<spdlog::logger> log_physics_frame;
std::shared_ptr<spdlog::logger> log_pointer;
std::shared_ptr<spdlog::logger> log_post_processing;
std::shared_ptr<spdlog::logger> log_programs;
std::shared_ptr<spdlog::logger> log_raytrace;
std::shared_ptr<spdlog::logger> log_raytrace_frame;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_rendertarget_imgui_windows;
std::shared_ptr<spdlog::logger> log_scene;
std::shared_ptr<spdlog::logger> log_scene_view;
std::shared_ptr<spdlog::logger> log_selection;
std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_svg;
std::shared_ptr<spdlog::logger> log_textures;
std::shared_ptr<spdlog::logger> log_tools;
std::shared_ptr<spdlog::logger> log_tree;
std::shared_ptr<spdlog::logger> log_tree_frame;
std::shared_ptr<spdlog::logger> log_trs_tool;
std::shared_ptr<spdlog::logger> log_trs_tool_frame;
std::shared_ptr<spdlog::logger> log_xr;
std::shared_ptr<spdlog::logger> log_graph_editor;

void initialize_logging()
{
    using namespace erhe::log;
    log_asset_browser              = make_logger      ("editor.asset_browser"             );
    log_startup                    = make_logger      ("editor.startup"                   );
    log_brush                      = make_logger      ("editor.brush"                     );
    log_composer                   = make_frame_logger("editor.composer"                  );
    log_controller_ray             = make_frame_logger("editor.controller_ray"            );
    log_debug_visualization        = make_frame_logger("editor.debug_visualization"       );
    log_draw                       = make_logger      ("editor.draw"                      );
    log_fly_camera                 = make_logger      ("editor.fly_camera"                );
    log_input_frame                = make_frame_logger("editor.input_frame"               );
    log_framebuffer                = make_frame_logger("editor.framebuffer"               );
    log_headset                    = make_logger      ("editor.headset"                   );
    log_hud                        = make_logger      ("editor.hud"                       );
    log_id_render                  = make_logger      ("editor.id_render"                 );
    log_materials                  = make_logger      ("editor.materials"                 );
    log_net                        = make_logger      ("editor.net"                       );
    log_node_properties            = make_logger      ("editor.node_properties"           );
    log_operations                 = make_logger      ("editor.operations"                );
    log_parsers                    = make_logger      ("editor.parsers"                   );
    log_physics                    = make_logger      ("editor.physics"                   );
    log_physics_frame              = make_frame_logger("editor.physics_frame"             );
    log_pointer                    = make_logger      ("editor.pointer"                   );
    log_post_processing            = make_frame_logger("editor.post_processing"           );
    log_programs                   = make_logger      ("editor.programs"                  );
    log_raytrace                   = make_logger      ("editor.raytrace"                  );
    log_raytrace_frame             = make_frame_logger("editor.raytrace_frame"            );
    log_render                     = make_frame_logger("editor.render"                    );
    log_rendertarget_imgui_windows = make_frame_logger("editor.rendertarget_imgui_windows");
    log_input                      = make_logger      ("editor.input"                     );
    log_scene                      = make_logger      ("editor.scene"                     );
    log_scene_view                 = make_frame_logger("editor.scene_view"                );
    log_selection                  = make_logger      ("editor.selection"                 );
    log_svg                        = make_logger      ("editor.svg"                       );
    log_textures                   = make_logger      ("editor.textures"                  );
    log_tools                      = make_logger      ("editor.tools"                     );
    log_trs_tool                   = make_logger      ("editor.transform_tool"            );
    log_trs_tool_frame             = make_frame_logger("editor.transform_tool_frame"      );
    log_xr                         = make_logger      ("editor.xr"                        );
    log_tree                       = make_logger      ("editor.tree"                      );
    log_tree_frame                 = make_frame_logger("editor.tree_frame"                );
    log_graph_editor               = make_frame_logger("editor.graph_editor"              );
}

}
