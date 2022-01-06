#include "erhe/physics/log.hpp"

namespace erhe::physics
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_physics{0.4f, 1.0f, 1.0f, Console_color::CYAN, Level::LEVEL_INFO};

} // namespace erhe::physics
