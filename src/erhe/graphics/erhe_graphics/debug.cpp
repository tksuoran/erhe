#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_graphics/debug.hpp"

#include "erhe_graphics/graphics_log.hpp"

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

Scoped_debug_group::Scoped_debug_group(
    const std::string_view debug_label
)
    : m_debug_label{debug_label}
{
    // gl::log_gl->trace("---- begin: {}", debug_label);
    // gl::push_debug_group(
    //     gl::Debug_source::debug_source_application,
    //     0,
    //     static_cast<GLsizei>(debug_label.length() + 1),
    //     debug_label.data()
    // );
}

Scoped_debug_group::~Scoped_debug_group() noexcept
{
    // gl::log_gl->trace("---- end: {}", m_debug_label);
    // gl::pop_debug_group();
}

} // namespace erhe::graphics
