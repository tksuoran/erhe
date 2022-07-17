#include "erhe/graphics/graphics_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::graphics
{

std::shared_ptr<spdlog::logger> log_buffer                   ;
std::shared_ptr<spdlog::logger> log_configuration            ;
std::shared_ptr<spdlog::logger> log_framebuffer              ;
std::shared_ptr<spdlog::logger> log_fragment_outputs         ;
std::shared_ptr<spdlog::logger> log_glsl                     ;
std::shared_ptr<spdlog::logger> log_load_png                 ;
std::shared_ptr<spdlog::logger> log_program                  ;
std::shared_ptr<spdlog::logger> log_save_png                 ;
std::shared_ptr<spdlog::logger> log_texture                  ;
std::shared_ptr<spdlog::logger> log_threads                  ;
std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
std::shared_ptr<spdlog::logger> log_vertex_stream            ;

void initialize_logging()
{
    log_buffer                   = erhe::log::make_logger("erhe::graphics::buffer",           spdlog::level::info);
    log_configuration            = erhe::log::make_logger("erhe::graphics::configuration",    spdlog::level::info);
    log_framebuffer              = erhe::log::make_logger("erhe::graphics::framebuffer",      spdlog::level::info);
    log_fragment_outputs         = erhe::log::make_logger("erhe::graphics::fragment_outputs", spdlog::level::info);
    log_glsl                     = erhe::log::make_logger("erhe::graphics::glsl",             spdlog::level::info);
    log_load_png                 = erhe::log::make_logger("erhe::graphics::load_png",         spdlog::level::info);
    log_program                  = erhe::log::make_logger("erhe::graphics::program",          spdlog::level::info);
    log_save_png                 = erhe::log::make_logger("erhe::graphics::save_png",         spdlog::level::info);
    log_texture                  = erhe::log::make_logger("erhe::graphics::texture",          spdlog::level::info);
    log_threads                  = erhe::log::make_logger("erhe::graphics::threads",          spdlog::level::info);
    log_vertex_attribute_mappings= erhe::log::make_logger("erhe::graphics::vertex_attribute", spdlog::level::info);
    log_vertex_stream            = erhe::log::make_logger("erhe::graphics::vertex_stream",    spdlog::level::info);
}

} // namespace erhe::graphics
