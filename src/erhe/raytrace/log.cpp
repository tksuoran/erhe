#include "erhe/raytrace/log.hpp"


namespace erhe::raytrace
{

using namespace erhe::log;

Category log_buffer  (Color::RED,    Color::GRAY, Level::LEVEL_WARN);
Category log_device  (Color::YELLOW, Color::GRAY, Level::LEVEL_WARN);
Category log_geometry(Color::GREEN,  Color::GRAY, Level::LEVEL_WARN);
Category log_scene   (Color::BLUE,   Color::GRAY, Level::LEVEL_WARN);

} // namespace erhe::raytrace
