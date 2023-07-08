#include "erhe/graphics/graphics_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::graphics
{

std::shared_ptr<spdlog::logger> log_buffer                   ;
std::shared_ptr<spdlog::logger> log_context                  ;
std::shared_ptr<spdlog::logger> log_debug                    ;
std::shared_ptr<spdlog::logger> log_framebuffer              ;
std::shared_ptr<spdlog::logger> log_fragment_outputs         ;
std::shared_ptr<spdlog::logger> log_glsl                     ;
std::shared_ptr<spdlog::logger> log_load_png                 ;
std::shared_ptr<spdlog::logger> log_program                  ;
std::shared_ptr<spdlog::logger> log_save_png                 ;
std::shared_ptr<spdlog::logger> log_shader_monitor           ;
std::shared_ptr<spdlog::logger> log_texture                  ;
std::shared_ptr<spdlog::logger> log_threads                  ;
std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
std::shared_ptr<spdlog::logger> log_vertex_stream            ;
std::shared_ptr<spdlog::logger> log_startup                  ;

void initialize_logging()
{
    log_buffer                    = erhe::log::make_logger("graphics::buffer",           spdlog::level::info);
    log_context                   = erhe::log::make_logger("graphics::context",          spdlog::level::info);
    log_debug                     = erhe::log::make_logger("graphics::debug",            spdlog::level::info);
    log_framebuffer               = erhe::log::make_logger("graphics::framebuffer",      spdlog::level::info);
    log_fragment_outputs          = erhe::log::make_logger("graphics::fragment_outputs", spdlog::level::info);
    log_glsl                      = erhe::log::make_logger("graphics::glsl",             spdlog::level::info);
    log_load_png                  = erhe::log::make_logger("graphics::load_png",         spdlog::level::info);
    log_program                   = erhe::log::make_logger("graphics::program",          spdlog::level::info);
    log_save_png                  = erhe::log::make_logger("graphics::save_png",         spdlog::level::info);
    log_shader_monitor            = erhe::log::make_logger("graphics::shader_monitor",   spdlog::level::info);
    log_texture                   = erhe::log::make_logger("graphics::texture",          spdlog::level::info);
    log_threads                   = erhe::log::make_logger("graphics::threads",          spdlog::level::info);
    log_vertex_attribute_mappings = erhe::log::make_logger("graphics::vertex_attribute", spdlog::level::info);
    log_vertex_stream             = erhe::log::make_logger("graphics::vertex_stream",    spdlog::level::info);
    log_startup                   = erhe::log::make_logger("graphics::startup",          spdlog::level::trace);
}

} // namespace erhe::graphics
