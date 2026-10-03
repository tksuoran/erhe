#include "erhe_graphics/gl/gl_scoped_debug_group.hpp"
#include "erhe_graphics/debug_label_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};
int  Scoped_debug_group_impl::s_max_message_length{0};
bool Scoped_debug_group_impl::s_clamp_to_max_length{false};

// GL has no per-cb concept; the active GL context is the recording
// target, so the cb argument is unused.
Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, const std::string_view debug_label)
{
    ERHE_VERIFY(!debug_label.empty());
    if (!s_enabled) {
        return;
    }
    log_debug->trace("---- begin: {}", debug_label);

    const Debug_label_buffer label{debug_label};
    GLsizei length = static_cast<GLsizei>(label.size() + 1);
    // NVIDIA driver bug workaround: glPushDebugGroup returns GL_INVALID_VALUE
    // when the message length is >= GL_MAX_DEBUG_MESSAGE_LENGTH, even though
    // OpenGL 4.6 spec section 22.2 conditions that error on "length is negative
    // AND ...". Filed at https://developer.nvidia.com/bugs/6216668. Clamp the
    // length we pass to GL so it stays below the cap (debug-group truncation
    // is a UI-only nicety).
    if (s_clamp_to_max_length && (length >= s_max_message_length)) {
        length = s_max_message_length - 1;
    }
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        length,
        label.c_str()
    );
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    if (!s_enabled) {
        return;
    }
    log_debug->trace("---- end");
    gl::pop_debug_group();
}

Scoped_queue_debug_group_impl::Scoped_queue_debug_group_impl(Device&, const std::string_view debug_label)
{
    ERHE_VERIFY(!debug_label.empty());
    if (!Scoped_debug_group_impl::s_enabled) {
        return;
    }
    log_debug->trace("---- begin: {}", debug_label);

    const Debug_label_buffer label{debug_label};
    GLsizei length = static_cast<GLsizei>(label.size() + 1);
    // Same NVIDIA length clamp as the cb-level scope above.
    if (Scoped_debug_group_impl::s_clamp_to_max_length && (length >= Scoped_debug_group_impl::s_max_message_length)) {
        length = Scoped_debug_group_impl::s_max_message_length - 1;
    }
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        length,
        label.c_str()
    );
}

Scoped_queue_debug_group_impl::~Scoped_queue_debug_group_impl() noexcept
{
    if (!Scoped_debug_group_impl::s_enabled) {
        return;
    }
    log_debug->trace("---- end");
    gl::pop_debug_group();
}

} // namespace erhe::graphics
