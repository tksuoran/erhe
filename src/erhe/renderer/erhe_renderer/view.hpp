#pragma once

#include <glm/glm.hpp>

namespace erhe::renderer {

class View
{
public:
    glm::mat4 clip_from_world       {1.0f};
    glm::vec4 viewport              {0.0f};
    glm::vec4 fov_sides             {-1.0f, 1.0f, 1.0f, -1.0f}; // left, right, up, down
    glm::vec4 view_position_in_world{0.0f}; // xyz used, w padding
};

} // namespace erhe::renderer
