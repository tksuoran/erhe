#include "log.hpp"

namespace editor {

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log_brush          {0.6f, 1.0f, 0.6f, Console_color::GREEN,   Level::LEVEL_INFO};
Category log_fly_camera     {1.0f, 0.9f, 0.0f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_framebuffer    {0.5f, 0.9f, 0.2f, Console_color::GREEN,   Level::LEVEL_INFO};
Category log_gl             {0.5f, 0.5f, 1.0f, Console_color::BLUE,    Level::LEVEL_INFO};
Category log_headset        {1.0f, 1.0f, 1.0f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_id_render      {1.0f, 0.6f, 1.0f, Console_color::MAGENTA, Level::LEVEL_INFO};
Category log_materials      {0.6f, 0.6f, 1.0f, Console_color::BLUE,    Level::LEVEL_INFO};
Category log_node_properties{0.2f, 0.9f, 0.2f, Console_color::GREEN,   Level::LEVEL_INFO};
Category log_parsers        {0.5f, 0.9f, 0.6f, Console_color::GREEN,   Level::LEVEL_INFO};
Category log_physics        {0.4f, 0.8f, 8.0f, Console_color::CYAN,    Level::LEVEL_INFO};
Category log_pointer        {0.7f, 0.7f, 0.7f, Console_color::GRAY,    Level::LEVEL_INFO};
Category log_programs       {0.9f, 0.9f, 1.0f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_raytrace       {1.0f, 1.0f, 0.4f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_render         {0.4f, 0.8f, 0.9f, Console_color::CYAN,    Level::LEVEL_INFO};
Category log_scene          {0.7f, 0.8f, 0.9f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_selection      {0.9f, 0.7f, 0.4f, Console_color::YELLOW,  Level::LEVEL_INFO};
Category log_svg            {0.9f, 1.0f, 0.9f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_textures       {0.9f, 1.0f, 1.0f, Console_color::WHITE,   Level::LEVEL_INFO};
Category log_trs_tool       {1.0f, 0.9f, 0.0f, Console_color::YELLOW,  Level::LEVEL_INFO};

}
