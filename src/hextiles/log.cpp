#include "log.hpp"

namespace hextiles {

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_map_window{0.6f, 1.0f, 0.0f, Console_color::GREEN, Level::LEVEL_INFO};
Category log_new_game  {0.6f, 1.0f, 0.0f, Console_color::GREEN, Level::LEVEL_INFO};

}
