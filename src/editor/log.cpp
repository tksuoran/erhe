#include "log.hpp"

namespace editor {

using namespace erhe::log;

Category log_startup     (Color::WHITE,   Color::GRAY, Level::LEVEL_INFO);
Category log_programs    (Color::WHITE,   Color::GRAY, Level::LEVEL_INFO);
Category log_textures    (Color::WHITE,   Color::GRAY, Level::LEVEL_INFO);
Category log_menu        (Color::YELLOW,  Color::GRAY, Level::LEVEL_INFO);
Category log_parsers     (Color::GREEN,   Color::GRAY, Level::LEVEL_INFO);
Category log_render      (Color::CYAN,    Color::GRAY, Level::LEVEL_INFO);
Category log_trs_tool    (Color::YELLOW,  Color::GRAY, Level::LEVEL_INFO);
Category log_tools       (Color::YELLOW,  Color::GRAY, Level::LEVEL_INFO);
Category log_selection   (Color::YELLOW,  Color::GRAY, Level::LEVEL_INFO);
Category log_id_render   (Color::MAGENTA, Color::GRAY, Level::LEVEL_INFO);
Category log_framebuffer (Color::GREEN,   Color::GRAY, Level::LEVEL_INFO);
Category log_pointer     (Color::GRAY,    Color::GRAY, Level::LEVEL_INFO);
Category log_input_events(Color::CYAN,    Color::GRAY, Level::LEVEL_INFO);
Category log_materials   (Color::BLUE,    Color::GRAY, Level::LEVEL_INFO);
Category log_renderdoc   (Color::GREEN,   Color::GRAY, Level::LEVEL_INFO);
Category log_brush       (Color::GREEN,   Color::GRAY, Level::LEVEL_INFO);
Category log_physics     (Color::CYAN,    Color::GRAY, Level::LEVEL_INFO);
Category log_gl          (Color::BLUE,    Color::GRAY, Level::LEVEL_INFO);
Category log_headset     (Color::WHITE,   Color::GRAY, Level::LEVEL_INFO);

}
