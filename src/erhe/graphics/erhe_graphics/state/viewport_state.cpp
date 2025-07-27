#include "erhe_graphics/state/viewport_state.hpp"

namespace erhe::graphics {

auto operator==(const Viewport_rect_state& lhs, const Viewport_rect_state& rhs) noexcept -> bool
{
    return
         (lhs.x      == rhs.x     ) &&
         (lhs.y      == rhs.y     ) &&
         (lhs.width  == rhs.width ) &&
         (lhs.height == rhs.height);
}

auto operator!=(const Viewport_rect_state& lhs, const Viewport_rect_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

//

auto operator==(const Viewport_depth_range_state& lhs, const Viewport_depth_range_state& rhs) noexcept -> bool
{
    return
        (lhs.min_depth == rhs.min_depth) &&
        (lhs.max_depth == rhs.max_depth);
}

auto operator!=(const Viewport_depth_range_state& lhs, const Viewport_depth_range_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
