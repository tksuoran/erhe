#pragma once

#include <glm/glm.hpp>

namespace erhe::scene
{

class Viewport
{
public:
    auto aspect_ratio() const -> float
    {
        return (height != 0.0f)
            ? (static_cast<float>(width) / static_cast<float>(height))
            : 1.0f;
    }

    // TODO
    auto unproject(
        const glm::mat4 world_from_clip,
        const glm::vec3 window,
        const float     depth_range_near,
        const float     depth_range_far
    ) const -> glm::vec3;

    // TODO
    auto project_to_screen_space(
        const glm::mat4 clip_from_world,
        const glm::vec3 position_in_world,
        const float     depth_range_near,
        const float     depth_range_far
    ) const -> glm::vec3;

    int  x     {0};
    int  y     {0};
    int  width {0};
    int  height{0};
    bool reverse_depth{true};
};

} // namespace erhe::scene
