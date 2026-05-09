#include "erhe_graphics/null/null_scoped_debug_group.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept = default;

} // namespace erhe::graphics
