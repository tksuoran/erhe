#include "editor_log.hpp"
#include "erhe/log/log.hpp"

namespace editor {

std::shared_ptr<spdlog::logger> log_brush;
std::shared_ptr<spdlog::logger> log_debug_visualization;
std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_fly_camera;
std::shared_ptr<spdlog::logger> log_framebuffer;
std::shared_ptr<spdlog::logger> log_gl;
std::shared_ptr<spdlog::logger> log_headset;
std::shared_ptr<spdlog::logger> log_id_render;
std::shared_ptr<spdlog::logger> log_materials;
std::shared_ptr<spdlog::logger> log_node_properties;
std::shared_ptr<spdlog::logger> log_parsers;
std::shared_ptr<spdlog::logger> log_physics;
std::shared_ptr<spdlog::logger> log_pointer;
std::shared_ptr<spdlog::logger> log_programs;
std::shared_ptr<spdlog::logger> log_raytrace;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_scene;
std::shared_ptr<spdlog::logger> log_selection;
std::shared_ptr<spdlog::logger> log_svg;
std::shared_ptr<spdlog::logger> log_textures;
std::shared_ptr<spdlog::logger> log_trs_tool;
std::shared_ptr<spdlog::logger> log_rendertarget_imgui_windows;

void initialize_logging()
{
    log_brush                      = erhe::log::make_logger("editor::brush"                     , spdlog::level::info);
    log_debug_visualization        = erhe::log::make_logger("editor::debug_visualization"       , spdlog::level::info, false);
    log_draw                       = erhe::log::make_logger("editor::draw"                      , spdlog::level::info);
    log_fly_camera                 = erhe::log::make_logger("editor::fly_camera"                , spdlog::level::info);
    log_framebuffer                = erhe::log::make_logger("editor::framebuffer"               , spdlog::level::info, false);
    log_gl                         = erhe::log::make_logger("editor::gl"                        , spdlog::level::info);
    log_headset                    = erhe::log::make_logger("editor::headset"                   , spdlog::level::info);
    log_id_render                  = erhe::log::make_logger("editor::id_render"                 , spdlog::level::info);
    log_materials                  = erhe::log::make_logger("editor::materials"                 , spdlog::level::info);
    log_node_properties            = erhe::log::make_logger("editor::node_properties"           , spdlog::level::info);
    log_parsers                    = erhe::log::make_logger("editor::parsers"                   , spdlog::level::info);
    log_physics                    = erhe::log::make_logger("editor::physics"                   , spdlog::level::info);
    log_pointer                    = erhe::log::make_logger("editor::pointer"                   , spdlog::level::info, false);
    log_programs                   = erhe::log::make_logger("editor::programs"                  , spdlog::level::info);
    log_raytrace                   = erhe::log::make_logger("editor::raytrace"                  , spdlog::level::warn);
    log_render                     = erhe::log::make_logger("editor::render"                    , spdlog::level::info, false);
    log_scene                      = erhe::log::make_logger("editor::scene"                     , spdlog::level::info);
    log_selection                  = erhe::log::make_logger("editor::selection"                 , spdlog::level::info);
    log_svg                        = erhe::log::make_logger("editor::svg"                       , spdlog::level::info);
    log_textures                   = erhe::log::make_logger("editor::textures"                  , spdlog::level::info);
    log_trs_tool                   = erhe::log::make_logger("editor::trs_tool"                  , spdlog::level::info);
    log_rendertarget_imgui_windows = erhe::log::make_logger("editor::rendertarget_imgui_windows", spdlog::level::info, false);
}

}
