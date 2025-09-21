#pragma once

#include <glm/glm.hpp>

namespace erhe::renderer {

class View
{
public:
    glm::mat4 clip_from_world;
    glm::vec4 viewport       ;
    glm::vec4 fov_sides      ; // left, right, up, down
};

} // namespace erhe::renderer
