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

Category log_gl{0.3f, 0.4f, 1.0f, Console_color::BLUE, Level::LEVEL_INFO};

static bool enable_error_checking = true;

void set_error_checking(const bool enable)
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

auto size_of_type(const gl::Draw_elements_type type) -> size_t
{
    switch (type)
    {
        using enum gl::Draw_elements_type;
        case unsigned_byte:  return 1;
        case unsigned_short: return 2;
        case unsigned_int:   return 4;
        default:
            ERHE_FATAL("Bad draw elements index type\n");
    }
}

auto size_of_type(const gl::Vertex_attrib_type type) -> size_t
{
    switch (type)
    {
        using enum gl::Vertex_attrib_type;
        case byte:
        case unsigned_byte:
        {
            return 1;
        }

        case half_float:
        case short_:
        case unsigned_short:
        {
            return 2;
        }

        case fixed:
        case float_:
        case int_:
        case int_2_10_10_10_rev:
        case unsigned_int:
        case unsigned_int_10f_11f_11f_rev:
        case unsigned_int_2_10_10_10_rev:
        {
            return 4;
        }

        case double_:
        {
            return 8;
        }

        default:
        {
            ERHE_FATAL("Bad vertex attribute type\n");
        }
    }
}

} // namespace gl
