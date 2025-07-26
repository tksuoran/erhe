#pragma once

#include <string>
#include <string_view>

typedef char GLchar;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;

namespace erhe::graphics {

void erhe_opengl_callback(
    GLenum        gl_source,
    GLenum        gl_type,
    GLuint        id,
    GLenum        gl_severity,
    GLsizei       /*length*/,
    const GLchar* message,
    const void*   /*userParam*/
);

void set_shader_source(const std::string& shader_source);

} // namespace erhe::graphics
