#include "erhe/graphics/log.hpp"

namespace erhe::graphics
{

using erhe::log::Log;

Log::Category log_buffer                   (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_configuration            (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_program                  (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_glsl                     (Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO, Log::Colorizer::glsl);
Log::Category log_vertex_stream            (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_vertex_attribute_mappings(Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_fragment_outputs         (Log::Color::GREEN,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_load_png                 (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_save_png                 (Log::Color::CYAN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);

} // namespace erhe::graphics
