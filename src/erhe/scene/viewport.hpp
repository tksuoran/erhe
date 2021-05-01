#pragma once

#include <glm/glm.hpp>

namespace erhe::scene
{

struct Viewport
{
    auto aspect_ratio() const
    -> float
    {
        return (height != 0.0f) ? (static_cast<float>(width) / static_cast<float>(height))
                                : 1.0f;
    }

    auto unproject(glm::mat4 world_from_clip,
                   glm::vec3 window,
                   float     depth_range_near,
                   float     depth_range_far)
    -> glm::vec3;

    auto project_to_screen_space(glm::mat4 clip_from_world,
                                 glm::vec3 position_in_world,
                                 float     depth_range_near,
                                 float     depth_range_far);

    int x     {0};
    int y     {0};
    int width {0};
    int height{0};
};

} // namespace erhe::scene
