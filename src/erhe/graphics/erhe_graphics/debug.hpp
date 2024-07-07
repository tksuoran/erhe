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

class Scoped_debug_group final
{
public:
    explicit Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group() noexcept;

private:
    std::string m_debug_label;
};

} // namespace erhe::graphics
