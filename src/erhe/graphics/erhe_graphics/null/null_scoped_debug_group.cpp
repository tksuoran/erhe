#include "erhe_graphics/null/null_scoped_debug_group.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, std::string_view)
{
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept = default;

Scoped_queue_debug_group_impl::Scoped_queue_debug_group_impl(Device&, std::string_view)
{
}

Scoped_queue_debug_group_impl::~Scoped_queue_debug_group_impl() noexcept = default;

} // namespace erhe::graphics
