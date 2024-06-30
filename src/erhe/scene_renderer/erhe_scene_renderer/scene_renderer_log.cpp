#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::scene_renderer {

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_program_interface;
std::shared_ptr<spdlog::logger> log_forward_renderer;
std::shared_ptr<spdlog::logger> log_shadow_renderer;
std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_material_buffer;
std::shared_ptr<spdlog::logger> log_primitive_buffer;

void initialize_logging()
{
    using namespace erhe::log;
    log_draw              = make_logger      ("erhe.scene_renderer.draw"                 );
    log_render            = make_frame_logger("erhe.scene_renderer.render"               );
    log_program_interface = make_logger      ("erhe.scene_renderer.log_program_interface");
    log_forward_renderer  = make_frame_logger("erhe.scene_renderer.forward_renderer"     );
    log_shadow_renderer   = make_frame_logger("erhe.scene_renderer.shadow_renderer"      );
    log_startup           = make_logger      ("erhe.scene_renderer.startup"              );
    log_material_buffer   = make_frame_logger("erhe.scene_renderer.material_buffer"      );
    log_primitive_buffer  = make_frame_logger("erhe.scene_renderer.primitive_buffer"     );
}

} // namespace erhe::scene_renderer
