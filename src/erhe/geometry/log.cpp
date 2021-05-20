#include "erhe/geometry/log.hpp"

namespace erhe::geometry
{

using erhe::log::Log;

Log::Category log                  (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_build_edges      (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_tangent_gen      (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_cone             (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_torus            (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_sphere           (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_polygon_texcoords(Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_interpolate      (Log::Color::WHITE,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_operation        (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_catmull_clark    (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_triangulate      (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_subdivide        (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_attribute_maps   (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_merge            (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_weld             (Log::Color::BLUE,   Log::Color::GRAY, Log::Level::LEVEL_INFO);

} // namespace erhe::geometry
