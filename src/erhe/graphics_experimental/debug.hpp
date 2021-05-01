#ifndef debug_hpp_erhe_graphics
#define debug_hpp_erhe_graphics

namespace erhe::graphics
{

void debug_program();
void debug_buffer(unsigned int target, unsigned int buffer, unsigned int index_type);
void debug_vao();
void debug_gl_state();

} // namespace erhe::graphics

#endif
