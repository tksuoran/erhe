#include "erhe_renderer/renderer_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::renderer {

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_buffer_writer;
std::shared_ptr<spdlog::logger> log_gpu_ring_buffer;

void initialize_logging()
{
    using namespace erhe::log;
    log_draw            = make_logger      ("erhe.renderer.draw"               );
    log_render          = make_frame_logger("erhe.renderer.render"             );
    log_startup         = make_logger      ("erhe.renderer.startup"            );
    log_buffer_writer   = make_frame_logger("erhe.renderer.buffer_writer"      );
    log_gpu_ring_buffer = make_frame_logger("erhe.renderer.log_gpu_ring_buffer");
}

} // namespace erhe::renderer
