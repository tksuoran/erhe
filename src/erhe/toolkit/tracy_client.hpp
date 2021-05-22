#pragma once

#if defined(TRACY_ENABLE)
#   include "erhe/gl/dynamic_load.hpp"
#   define glGenQueries          gl::glGenQueries
#   define glGetInteger64v       gl::glGetInteger64v
#   define glGetQueryiv          gl::glGetQueryiv
#   define glGetQueryObjectiv    gl::glGetQueryObjectiv
#   define glGetQueryObjectui64v gl::glGetQueryObjectui64v
#   define glQueryCounter        gl::glQueryCounter
#endif

#include "Tracy.hpp"
#include "TracyOpenGL.hpp"

#if defined(TRACY_ENABLE)
#   undef glGenQueries
#   undef glGetInteger64v
#   undef glGetQueryiv
#   undef glGetQueryObjectiv
#   undef glGetQueryObjectui64v
#   undef glQueryCounter
#endif
