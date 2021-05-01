#ifndef log_hpp_erhe_graphics
#define log_hpp_erhe_graphics

#include "erhe/log/log.hpp"

namespace erhe::graphics
{

extern erhe::log::Log::Category log_buffer;
extern erhe::log::Log::Category log_configuration;
extern erhe::log::Log::Category log_program;
extern erhe::log::Log::Category log_glsl;
//extern erhe::log::Log::Category log_render_group;
extern erhe::log::Log::Category log_vertex_stream;
extern erhe::log::Log::Category log_vertex_attribute_mappings;
extern erhe::log::Log::Category log_fragment_outputs;
extern erhe::log::Log::Category log_load_png;
extern erhe::log::Log::Category log_save_png;

} // namespace erhe::graphics

#endif
