#include "erhe/ui/log.hpp"

namespace erhe::ui
{

std::shared_ptr<spdlog::logger> log_text_buffer;
std::shared_ptr<spdlog::logger> log_font       ;

void initialize_logging()
{
    log_text_buffer = erhe::log::make_logger("erhe::ui::text_buffer", spdlog::level::info);
    log_font        = erhe::log::make_logger("erhe::ui::font",        spdlog::level::info);
}

} // namespace erhe::ui
