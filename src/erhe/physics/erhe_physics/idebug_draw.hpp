#pragma once

#include <glm/glm.hpp>

namespace erhe::physics {

// Line sink for IWorld::debug_draw(). Implemented by the application on top
// of its line renderer, so erhe::physics needs no rendering dependency.
class IDebug_draw
{
public:
    virtual ~IDebug_draw() noexcept;

    virtual void draw_line(glm::vec3 from, glm::vec3 to, glm::vec4 color) = 0;
};

} // namespace erhe::physics
