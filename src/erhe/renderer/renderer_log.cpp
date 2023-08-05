#include "erhe/renderer/renderer_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::renderer
{

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_buffer_writer;
std::shared_ptr<spdlog::logger> log_multi_buffer;

void initialize_logging()
{
    using namespace erhe::log;
    log_draw          = make_logger      ("erhe.renderer.draw"         );
    log_render        = make_frame_logger("erhe.renderer.render"       );
    log_startup       = make_logger      ("erhe.renderer.startup"      );
    log_buffer_writer = make_frame_logger("erhe.renderer.buffer_writer");
    log_multi_buffer  = make_frame_logger("erhe.renderer.multi_buffer" );
}

} // namespace erhe::renderer
