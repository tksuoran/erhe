#include "erhe/gl/gl.hpp"
#include "erhe/log/log.hpp"
#include "erhe/gl/enum_string_functions.hpp"
//#if !defined(ERHE_WINDOW_LIBRARY_MANGO)
#include "erhe/gl/dynamic_load.hpp"
//#endif
#include "erhe/toolkit/verify.hpp"

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <iostream>

#if !defined(WIN32)
# include <csignal>
#endif

namespace gl
{

using namespace erhe::log;

Category log_gl(Color::YELLOW, Color::GRAY, Level::LEVEL_INFO);

static bool enable_error_checking = true;

void set_error_checking(bool enable)
{
    enable_error_checking = enable;
}

void check_error()
{
    if (!enable_error_checking)
    {
        return;
    }

#if defined(ERHE_DLOAD_ALL_GL_SYMBOLS)
    auto error_code = static_cast<gl::Error_code>(gl::glGetError());
#else
    auto error_code = static_cast<gl::Error_code>(glGetError());
#endif

    if (error_code != gl::Error_code::no_error)
    {
        log_gl.error("{}\n", gl::c_str(error_code));
#if defined(WIN32)
        DebugBreak();
#else
        raise(SIGTRAP);
#endif
    }
}

auto size_of_type(gl::Draw_elements_type type)
-> size_t
{
    switch (type)
    {
        case gl::Draw_elements_type::unsigned_byte:  return 1;
        case gl::Draw_elements_type::unsigned_short: return 2;
        case gl::Draw_elements_type::unsigned_int:   return 4;
        default:
            FATAL("Bad draw elements index type");
    }
}

auto size_of_type(gl::Vertex_attrib_type type)
-> size_t
{
    switch (type)
    {
        case gl::Vertex_attrib_type::byte:
        case gl::Vertex_attrib_type::unsigned_byte:
        {
            return 1;
        }

        case gl::Vertex_attrib_type::half_float:
        case gl::Vertex_attrib_type::short_:
        case gl::Vertex_attrib_type::unsigned_short:
        {
            return 2;
        }

        case gl::Vertex_attrib_type::fixed:
        case gl::Vertex_attrib_type::float_:
        case gl::Vertex_attrib_type::int_:
        case gl::Vertex_attrib_type::int_2_10_10_10_rev:
        case gl::Vertex_attrib_type::unsigned_int:
        case gl::Vertex_attrib_type::unsigned_int_10f_11f_11f_rev:
        case gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev:
        {
            return 4;
        }

        case gl::Vertex_attrib_type::double_:
        {
            return 8;
        }

        default:
        {
            FATAL("Bad vertex attribute type");
        }
    }
}

} // namespace gl
