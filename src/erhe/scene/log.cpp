#include "erhe/scene/log.hpp"

namespace erhe::scene
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log{1.0f, 1.0f, 0.4f, Console_color::YELLOW, Level::LEVEL_INFO};

} // namespace erhe::scene
