#include "erhe/primitive/log.hpp"


namespace erhe::primitive
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_primitive_builder{1.0f, 0.4f, 0.2f, Console_color::RED,     Level::LEVEL_INFO};
Category log_primitive        {0.7f, 0.4f, 1.0f, Console_color::MAGENTA, Level::LEVEL_INFO};

} // namespace erhe::primitive
