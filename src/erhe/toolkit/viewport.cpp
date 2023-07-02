#include "erhe/toolkit/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace erhe::toolkit
{

auto Viewport::aspect_ratio() const -> float
{
    return (height != 0.0f)
        ? (static_cast<float>(width) / static_cast<float>(height))
        : 1.0f;
}

auto Viewport::unproject(
    const glm::mat4& world_from_clip,
    const glm::vec3& window,
    const float      depth_range_near,
    const float      depth_range_far
) const -> std::optional<glm::vec3>
{
    return erhe::toolkit::unproject(
        world_from_clip,
        window,
        depth_range_near,
        depth_range_far,
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)
    );
}

auto Viewport::project_to_screen_space(
    const glm::mat4& clip_from_world,
    const glm::vec3& position_in_world,
    const float      depth_range_near,
    const float      depth_range_far
) const -> glm::vec3
{
    return erhe::toolkit::project_to_screen_space(
        clip_from_world,
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)
    );
}

auto Viewport::hit_test(const int px, const int py) const -> bool
{
    return
        (px >= x) &&
        (py >= y) &&
        (px <= x + width) &&
        (py <= y + height);
}

} // namespace erhe::toolkit
