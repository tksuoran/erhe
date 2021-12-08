#ifndef gl_hpp_erhe_gl
#define gl_hpp_erhe_gl

#if defined(_WIN32)
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   define VC_EXTRALEAN
#   ifndef STRICT
#       define STRICT
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX       // Macros min(a,b) and max(a,b)
#   endif
//#   include <windows.h>
#endif

#include "erhe/gl/wrapper_functions.hpp"

#include "erhe/log/log.hpp"


namespace gl
{

extern erhe::log::Category log_gl;

[[nodiscard]] auto size_of_type(const gl::Draw_elements_type type) -> size_t;
[[nodiscard]] auto size_of_type(const gl::Vertex_attrib_type type) -> size_t;

void set_error_checking(const bool enable);
void check_error       ();

} // namespace gl

#endif
