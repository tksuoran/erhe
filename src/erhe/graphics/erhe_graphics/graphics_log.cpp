#include "erhe_graphics/graphics_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::graphics {

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
std::shared_ptr<spdlog::logger> log_texture_frame            ;
std::shared_ptr<spdlog::logger> log_threads                  ;
std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
std::shared_ptr<spdlog::logger> log_vertex_stream            ;
std::shared_ptr<spdlog::logger> log_render_pass              ;
std::shared_ptr<spdlog::logger> log_startup                  ;

void initialize_logging()
{
    using namespace erhe::log;
    log_buffer                    = make_logger      ("erhe.graphics.buffer"          );
    log_context                   = make_logger      ("erhe.graphics.context"         );
    log_debug                     = make_logger      ("erhe.graphics.debug"           );
    log_framebuffer               = make_logger      ("erhe.graphics.framebuffer"     );
    log_fragment_outputs          = make_logger      ("erhe.graphics.fragment_outputs");
    log_glsl                      = make_logger      ("erhe.graphics.glsl"            );
    log_load_png                  = make_logger      ("erhe.graphics.load_png"        );
    log_program                   = make_logger      ("erhe.graphics.program"         );
    log_save_png                  = make_logger      ("erhe.graphics.save_png"        );
    log_shader_monitor            = make_logger      ("erhe.graphics.shader_monitor"  );
    log_texture                   = make_logger      ("erhe.graphics.texture"         );
    log_texture_frame             = make_frame_logger("erhe.graphics.texture_frame"   );
    log_threads                   = make_logger      ("erhe.graphics.threads"         );
    log_vertex_attribute_mappings = make_logger      ("erhe.graphics.vertex_attribute");
    log_vertex_stream             = make_logger      ("erhe.graphics.vertex_stream"   );
    log_render_pass               = make_frame_logger("erhe.graphics.render_pass"     );
    log_startup                   = make_logger      ("erhe.graphics.startup"         );
}

} // namespace erhe::graphics
