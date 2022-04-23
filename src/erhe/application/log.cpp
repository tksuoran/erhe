#include "log.hpp"

namespace erhe::application {

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_command_state_transition{0.8f, 0.9f, 1.0f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_input                   {0.9f, 0.8f, 0.3f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_input_event             {0.9f, 0.9f, 5.0f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_input_event_consumed    {1.0f, 1.0f, 8.0f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_input_event_filtered    {1.0f, 0.8f, 7.0f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_input_events            {0.4f, 0.9f, 1.0f, Console_color::CYAN,    Level::LEVEL_INFO};
Category log_performance             {0.9f, 0.5f, 0.1f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_renderdoc               {0.3f, 1.0f, 0.4f, Console_color::GREEN,   Level::LEVEL_INFO};
Category log_startup                 {0.9f, 1.0f, 0.9f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_tools                   {0.9f, 1.0f, 0.1f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_windows                 {1.0f, 0.9f, 0.0f, Console_color::YELLOW,  Level::LEVEL_INFO};

}
