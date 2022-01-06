#include "erhe/geometry/log.hpp"

namespace erhe::geometry
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_geometry         {1.0f, 1.0f, 0.0f, Console_color::YELLOW, Level::LEVEL_WARN};
Category log_build_edges      {0.9f, 0.8f, 0.4f, Console_color::YELLOW, Level::LEVEL_WARN};
Category log_tangent_gen      {0.8f, 0.8f, 0.2f, Console_color::YELLOW, Level::LEVEL_WARN};
Category log_cone             {0.4f, 0.9f, 0.2f, Console_color::GREEN,  Level::LEVEL_WARN};
Category log_torus            {0.2f, 0.9f, 0.4f, Console_color::GREEN,  Level::LEVEL_WARN};
Category log_sphere           {0.3f, 0.9f, 0.3f, Console_color::GREEN,  Level::LEVEL_WARN};
Category log_polygon_texcoords{0.0f, 1.0f, 1.0f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_interpolate      {0.9f, 0.9f, 0.8f, Console_color::WHITE,  Level::LEVEL_WARN};
Category log_operation        {0.3f, 0.8f, 0.8f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_catmull_clark    {0.2f, 0.9f, 0.8f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_triangulate      {0.4f, 0.9f, 0.9f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_subdivide        {0.1f, 1.0f, 0.8f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_attribute_maps   {0.1f, 0.8f, 1.0f, Console_color::CYAN,   Level::LEVEL_WARN};
Category log_merge            {0.7f, 1.0f, 0.1f, Console_color::YELLOW, Level::LEVEL_WARN};
Category log_weld             {0.4f, 0.4f, 1.0f, Console_color::BLUE,   Level::LEVEL_WARN};

} // namespace erhe::geometry
