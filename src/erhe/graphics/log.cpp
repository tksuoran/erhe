#include "erhe/graphics/log.hpp"

namespace erhe::graphics
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;
using Colorizer     = erhe::log::Colorizer;

Category log_buffer                   {0.9f, 0.9f, 0.6f,Console_color::YELLOW, Level::LEVEL_INFO};
Category log_configuration            {0.4f, 1.0f, 0.3f,Console_color::GREEN,  Level::LEVEL_INFO};
Category log_framebuffer              {0.2f, 0.9f, 0.4f,Console_color::GREEN,  Level::LEVEL_INFO};
Category log_fragment_outputs         {0.0f, 1.0f, 0.0f,Console_color::GREEN,  Level::LEVEL_INFO};
Category log_glsl                     {0.8f, 0.6f, 0.4f,Console_color::YELLOW, Level::LEVEL_INFO, Colorizer::glsl};
Category log_load_png                 {0.5f, 0.8f, 1.0f,Console_color::CYAN,   Level::LEVEL_INFO};
Category log_program                  {1.0f, 0.9f, 0.8f,Console_color::YELLOW, Level::LEVEL_INFO};
Category log_save_png                 {0.6f, 1.0f, 1.0f,Console_color::CYAN,   Level::LEVEL_INFO};
Category log_texture                  {0.5f, 0.4f, 1.0f,Console_color::BLUE,   Level::LEVEL_INFO};
Category log_threads                  {0.0f, 0.0f, 1.0f,Console_color::BLUE,   Level::LEVEL_INFO};
Category log_vertex_attribute_mappings{0.3f, 1.0f, 0.0f,Console_color::GREEN,  Level::LEVEL_INFO};
Category log_vertex_stream            {0.0f, 1.0f, 0.3f,Console_color::GREEN,  Level::LEVEL_INFO};

} // namespace erhe::graphics
