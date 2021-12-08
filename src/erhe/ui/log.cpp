#include "erhe/ui/log.hpp"

namespace erhe::ui
{

using namespace erhe::log;

Category log_text_buffer{Color::GREEN,  Color::GRAY, Level::LEVEL_WARN};
Category log_font       {Color::YELLOW, Color::GRAY, Level::LEVEL_WARN};

} // namespace erhe::ui
