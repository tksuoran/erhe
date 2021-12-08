#include "erhe/geometry/log.hpp"

namespace erhe::geometry
{

using namespace erhe::log;

Category log_geometry         {Color::YELLOW, Color::GRAY, Level::LEVEL_WARN};
Category log_build_edges      {Color::YELLOW, Color::GRAY, Level::LEVEL_WARN};
Category log_tangent_gen      {Color::YELLOW, Color::GRAY, Level::LEVEL_WARN};
Category log_cone             {Color::GREEN,  Color::GRAY, Level::LEVEL_WARN};
Category log_torus            {Color::GREEN,  Color::GRAY, Level::LEVEL_WARN};
Category log_sphere           {Color::GREEN,  Color::GRAY, Level::LEVEL_WARN};
Category log_polygon_texcoords{Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_interpolate      {Color::WHITE,  Color::GRAY, Level::LEVEL_WARN};
Category log_operation        {Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_catmull_clark    {Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_triangulate      {Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_subdivide        {Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_attribute_maps   {Color::CYAN,   Color::GRAY, Level::LEVEL_WARN};
Category log_merge            {Color::YELLOW, Color::GRAY, Level::LEVEL_WARN};
Category log_weld             {Color::BLUE,   Color::GRAY, Level::LEVEL_WARN};

} // namespace erhe::geometry
