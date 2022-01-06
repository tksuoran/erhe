#include "erhe/ui/log.hpp"

namespace erhe::ui
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_text_buffer{0.1f, 1.0f, 0.6f, Console_color::GREEN,  Level::LEVEL_INFO};
Category log_font       {0.9f, 1.0f, 0.6f, Console_color::YELLOW, Level::LEVEL_INFO};

} // namespace erhe::ui
