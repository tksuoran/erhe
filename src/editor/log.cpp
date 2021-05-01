#include "log.hpp"

namespace sample {

using erhe::log::Log;

Log::Category log_startup     (Log::Color::WHITE,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_programs    (Log::Color::WHITE,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_textures    (Log::Color::WHITE,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_menu        (Log::Color::YELLOW,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_parsers     (Log::Color::GREEN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_render      (Log::Color::CYAN,    Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_trs_tool    (Log::Color::YELLOW,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_tools       (Log::Color::YELLOW,  Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_id_render   (Log::Color::MAGENTA, Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_framebuffer (Log::Color::GREEN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_pointer     (Log::Color::GRAY,    Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_input_events(Log::Color::CYAN,    Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_materials   (Log::Color::BLUE,    Log::Color::GRAY, Log::Level::LEVEL_INFO);
Log::Category log_renderdoc   (Log::Color::GREEN,   Log::Color::GRAY, Log::Level::LEVEL_INFO);

}
