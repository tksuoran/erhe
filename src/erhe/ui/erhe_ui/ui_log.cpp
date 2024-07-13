#include "erhe_ui/ui_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::ui {

std::shared_ptr<spdlog::logger> log_text_buffer;
std::shared_ptr<spdlog::logger> log_font;

void initialize_logging()
{
    using namespace erhe::log;
    log_text_buffer = make_logger("erhe.ui.text_buffer");
    log_font        = make_logger("erhe.ui.font"       );
}

} // namespace erhe::ui
