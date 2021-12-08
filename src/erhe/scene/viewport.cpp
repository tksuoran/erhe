#include "erhe/scene/viewport.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace erhe::scene
{

auto Viewport::unproject(
    const glm::mat4 world_from_clip,
    const glm::vec3 window,
    const float     depth_range_near,
    const float     depth_range_far
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
    const glm::mat4 clip_from_world,
    const glm::vec3 position_in_world,
    const float     depth_range_near,
    const float     depth_range_far
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

} // namespace erhe::scene
