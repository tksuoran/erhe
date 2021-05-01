#include "erhe/ui/log.hpp"

namespace erhe::ui
{

using erhe::log::Log;

Log::Category log_gui_renderer   (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_render_group   (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_button         (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_ninepatch      (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_ninepatch_style(Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_text_buffer    (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_font           (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_layout         (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_WARN);

} // namespace erhe::ui
