{AUTOGENERATION_WARNING}

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/dynamic_load.hpp"
#include "erhe_gl/gl_log.hpp"
#include "erhe_gl/gl_helpers.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <memory>

namespace gl {{

// To enable GL error checking after every GL call, define ERHE_GL_CHECK_ERRORS
// before including this file, or pass it via CMake. Enabled by default on macOS
// debug builds.
#if !defined(ERHE_GL_CHECK_ERRORS) && defined(ERHE_OS_OSX) && !defined(NDEBUG)
#   define ERHE_GL_CHECK_ERRORS
#endif

#if defined(ERHE_GL_CHECK_ERRORS)
#   define ERHE_CHECK_GL_ERRORS gl_helpers::check_error();
#else
#   define ERHE_CHECK_GL_ERRORS
#endif

//#define ERHE_LOG_GL_FUNCTIONS

auto glbitfield(const ::GLbitfield value) -> ::GLbitfield
{{
    return value;
}}

{ENUM_HELPER_DEFINITIONS}

{WRAPPER_FUNCTION_DEFINITIONS}

}} // namespace gl
