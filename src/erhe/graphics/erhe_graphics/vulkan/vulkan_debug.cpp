#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Scoped_debug_group::Scoped_debug_group(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
}

Scoped_debug_group::~Scoped_debug_group() noexcept
{
}

} // namespace erhe::graphics
