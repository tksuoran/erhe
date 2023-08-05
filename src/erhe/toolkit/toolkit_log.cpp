#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::toolkit
{

std::shared_ptr<spdlog::logger> log_file;
std::shared_ptr<spdlog::logger> log_space_mouse;
std::shared_ptr<spdlog::logger> log_sleep;
std::shared_ptr<spdlog::logger> log_window;
std::shared_ptr<spdlog::logger> log_window_event;
std::shared_ptr<spdlog::logger> log_renderdoc;

void initialize_logging()
{
    using namespace erhe::log;
    log_file         = make_logger("erhe.toolkit.file"        );
    log_space_mouse  = make_logger("erhe.toolkit.space_mouse" );
    log_sleep        = make_logger("erhe.toolkit.sleep"       );
    log_window       = make_logger("erhe.toolkit.window"      );
    log_window_event = make_logger("erhe.toolkit.window_event");
    log_renderdoc    = make_logger("erhe.toolkit.renderdoc"   );
}

} // namespace erhe::toolkit
