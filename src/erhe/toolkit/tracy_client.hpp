#pragma once

#if defined(TRACY_ENABLE) && defined(ERHE_TRACY_GL)
#   include "erhe/gl/dynamic_load.hpp"
#   define glGenQueries          gl::glGenQueries
#   define glGetInteger64v       gl::glGetInteger64v
#   define glGetQueryiv          gl::glGetQueryiv
#   define glGetQueryObjectiv    gl::glGetQueryObjectiv
#   define glGetQueryObjectui64v gl::glGetQueryObjectui64v
#   define glQueryCounter        gl::glQueryCounter
#endif

#include <Tracy.hpp>

#if defined(ERHE_TRACY_GL)
#   include "TracyOpenGL.hpp"
#endif

#if defined(TRACY_ENABLE) && defined(ERHE_TRACY_GL)
#   undef glGenQueries
#   undef glGetInteger64v
#   undef glGetQueryiv
#   undef glGetQueryObjectiv
#   undef glGetQueryObjectui64v
#   undef glQueryCounter
#endif
