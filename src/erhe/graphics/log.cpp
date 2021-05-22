#include "erhe/graphics/log.hpp"

namespace erhe::graphics
{

using namespace erhe::log;

Category log_buffer                   (Color::YELLOW, Color::GRAY, Level::LEVEL_INFO);
Category log_configuration            (Color::GREEN,  Color::GRAY, Level::LEVEL_INFO);
Category log_program                  (Color::YELLOW, Color::GRAY, Level::LEVEL_INFO);
Category log_glsl                     (Color::YELLOW, Color::GRAY, Level::LEVEL_INFO, Colorizer::glsl);
Category log_vertex_stream            (Color::GREEN,  Color::GRAY, Level::LEVEL_INFO);
Category log_vertex_attribute_mappings(Color::GREEN,  Color::GRAY, Level::LEVEL_INFO);
Category log_fragment_outputs         (Color::GREEN,  Color::GRAY, Level::LEVEL_INFO);
Category log_load_png                 (Color::CYAN,   Color::GRAY, Level::LEVEL_INFO);
Category log_save_png                 (Color::CYAN,   Color::GRAY, Level::LEVEL_INFO);

} // namespace erhe::graphics
