#include "erhe_window/window_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::window {

std::shared_ptr<spdlog::logger> log_space_mouse;
std::shared_ptr<spdlog::logger> log_window;
std::shared_ptr<spdlog::logger> log_window_event;
std::shared_ptr<spdlog::logger> log_renderdoc;

void initialize_logging()
{
    using namespace erhe::log;
    log_space_mouse  = make_logger("erhe.window.space_mouse" );
    log_window       = make_logger("erhe.window.window"      );
    log_window_event = make_logger("erhe.window.window_event");
    log_renderdoc    = make_logger("erhe.window.renderdoc"   );
}

}
