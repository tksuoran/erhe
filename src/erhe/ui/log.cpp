#include "erhe/ui/log.hpp"

namespace erhe::ui
{

using namespace erhe::log;

Category log_gui_renderer   (Color::GREEN,  Color::GRAY, Level::LEVEL_WARN);
Category log_render_group   (Color::CYAN,   Color::GRAY, Level::LEVEL_WARN);
Category log_button         (Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);
Category log_ninepatch      (Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);
Category log_ninepatch_style(Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);
Category log_text_buffer    (Color::GREEN,  Color::GRAY, Level::LEVEL_WARN);
Category log_font           (Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);
Category log_layout         (Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);

} // namespace erhe::ui
