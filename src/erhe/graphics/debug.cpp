#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe/graphics/debug.hpp"

#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/graphics_log.hpp"

#if !defined(WIN32)
#   include <signal.h>
#endif

// Comment this out disable timer queries
#define ERHE_USE_TIME_QUERY 1

namespace gl
{

extern std::shared_ptr<spdlog::logger> log_gl;

}


namespace erhe::graphics
{

void erhe_opengl_callback(
    GLenum        gl_source,
    GLenum        gl_type,
    GLuint        id,
    GLenum        gl_severity,
    GLsizei       /*length*/,
    const GLchar* message,
    const void*   /*userParam*/
)
{
    // Intel:
    // source:   GL_DEBUG_SOURCE_API
    // type:     GL_DEBUG_TYPE_PERFORMANCE
    // id:       0x000008
    // severity: GL_DEBUG_SEVERITY_LOW:
    //
    // API_ID_REDUNDANT_FBO performance warning has been generated.
    // Redundant state change in glBindFramebuffer API call, FBO 0, "", already bound.
    //
    // GL debug messsage:
    // source:   GL_DEBUG_SOURCE_API
    // type:     GL_DEBUG_TYPE_PERFORMANCE
    // id:       0x000002
    // severity: GL_DEBUG_SEVERITY_MEDIUM
    // API_ID_RECOMPILE_FRAGMENT_SHADER performance warning has been generated. Fragment shader recompiled due to state change.
    //
    // Nvidia:
    // source:   GL_DEBUG_SOURCE_API
    // type:     GL_DEBUG_TYPE_PERFORMANCE
    // id:       0x020092
    // severity: GL_DEBUG_SEVERITY_MEDIUM
    // Program/shader state performance warning: Fragment shader in program 63 is being recompiled based on GL state.
    if (
        (id == 0x020071) ||
        (id == 0x020061) ||
        (id == 0x020092) ||
        (id == 0x000008) ||
        (id == 0x000002)
    ) {
        return;
    }
    const auto severity = static_cast<gl::Debug_severity>(gl_severity);
    const auto type     = static_cast<gl::Debug_type    >(gl_type);
    const auto source   = static_cast<gl::Debug_source  >(gl_source);
    if (source == gl::Debug_source::debug_source_application) {
        return;
    }

    log_debug->info(
        "GL debug message:\n"
        "source:   {}\n"
        "type:     {}\n"
        "id:       {:#08x}\n"
        "severity: {}\n"
        "{}",
        gl::c_str(source),
        gl::c_str(type),
        id,
        gl::c_str(severity),
        (message != nullptr)
            ? message
            : ""
    );

    if (severity == gl::Debug_severity::debug_severity_high) {
#if defined(WIN32)
        DebugBreak();
#else
        raise(SIGTRAP);
#endif
    }
}

Scoped_debug_group::Scoped_debug_group(
    const std::string_view debug_label
)
    : m_debug_label{debug_label}
{
    gl::log_gl->trace("---- begin: {}", debug_label);
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(debug_label.length() + 1),
        debug_label.data()
    );
}

Scoped_debug_group::~Scoped_debug_group() noexcept
{
    gl::log_gl->trace("---- end: {}", m_debug_label);
    gl::pop_debug_group();
}

} // namespace erhe::graphics
