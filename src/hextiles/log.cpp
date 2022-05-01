#include "log.hpp"

namespace hextiles {

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_map_window   {0.6f, 1.0f, 0.0f, Console_color::GREEN,  Level::LEVEL_INFO};
Category log_map_generator{0.4f, 1.0f, 0.6f, Console_color::CYAN,   Level::LEVEL_INFO};
Category log_map_editor   {1.0f, 0.5f, 0.2f, Console_color::YELLOW, Level::LEVEL_INFO};
Category log_new_game     {0.6f, 1.0f, 0.0f, Console_color::GREEN,  Level::LEVEL_INFO};
Category log_tiles        {0.0f, 0.0f, 1.0f, Console_color::BLUE,   Level::LEVEL_INFO};

}
