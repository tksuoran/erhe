#include "erhe_graphics/graphics_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::graphics {

std::shared_ptr<spdlog::logger> log_context    ;
std::shared_ptr<spdlog::logger> log_swapchain  ;
std::shared_ptr<spdlog::logger> log_debug      ;
std::shared_ptr<spdlog::logger> log_framebuffer;
std::shared_ptr<spdlog::logger> log_threads    ;
std::shared_ptr<spdlog::logger> log_render_pass;
std::shared_ptr<spdlog::logger> log_startup    ;

void initialize_logging()
{
    using namespace erhe::log;
    log_context                   = make_logger      ("erhe.graphics.context"    , spdlog::level::info);
    log_swapchain                 = make_logger      ("erhe.graphics.swapchain"  , spdlog::level::info);
    log_debug                     = make_logger      ("erhe.graphics.debug"      , spdlog::level::info);
    log_framebuffer               = make_logger      ("erhe.graphics.framebuffer", spdlog::level::info);
    log_threads                   = make_logger      ("erhe.graphics.threads"    , spdlog::level::info);
    log_render_pass               = make_frame_logger("erhe.graphics.render_pass", spdlog::level::info);
    log_startup                   = make_logger      ("erhe.graphics.startup"    , spdlog::level::info);
}

} // namespace erhe::graphics
