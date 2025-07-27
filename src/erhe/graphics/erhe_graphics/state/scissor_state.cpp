#include "erhe_graphics/state/scissor_state.hpp"

namespace erhe::graphics {

auto operator==(const Scissor_state& lhs, const Scissor_state& rhs) noexcept -> bool
{
    return
        (lhs.x      == rhs.x     ) &&
        (lhs.y      == rhs.y     ) &&
        (lhs.width  == rhs.width ) &&
        (lhs.height == rhs.height);
}

auto operator!=(const Scissor_state& lhs, const Scissor_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
