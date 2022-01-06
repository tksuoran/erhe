#include "erhe/components/log.hpp"

namespace erhe::components
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_components{0.8f, 0.8f, 0.8f, Console_color::WHITE, Level::LEVEL_INFO};

} // namespace erhe::components
