#include "erhe/raytrace/log.hpp"


namespace erhe::raytrace
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_buffer  {1.0f, 0.3f, 0.4f, Console_color::RED,    Level::LEVEL_WARN};
Category log_device  {1.0f, 1.0f, 0.0f, Console_color::YELLOW, Level::LEVEL_WARN};
Category log_geometry{0.0f, 1.0f, 0.0f, Console_color::GREEN,  Level::LEVEL_WARN};
Category log_scene   {0.0f, 0.0f, 1.0f, Console_color::BLUE,   Level::LEVEL_WARN};
Category log_embree  {0.0f, 1.0f, 0.0f, Console_color::GREEN,  Level::LEVEL_INFO};

} // namespace erhe::raytrace
