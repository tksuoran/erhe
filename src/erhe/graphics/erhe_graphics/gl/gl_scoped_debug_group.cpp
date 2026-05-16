#include "erhe_graphics/gl/gl_scoped_debug_group.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

// GL has no per-cb concept; the active GL context is the recording
// target, so the cb argument is unused.
Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
    ERHE_VERIFY(!m_debug_label.empty());
    if (!s_enabled) {
        return;
    }
    log_debug->trace("---- begin: {}", m_debug_label.string_view());
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(m_debug_label.size() + 1),
        m_debug_label.data()
    );
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    if (!s_enabled) {
        return;
    }
    log_debug->trace("---- end: {}", m_debug_label.string_view());
    gl::pop_debug_group();
}

} // namespace erhe::graphics
