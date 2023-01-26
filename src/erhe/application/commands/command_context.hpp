#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::application {

class Input_arguments
{
public:
    uint32_t  button_bits        {0u};
    glm::vec2 vec2_absolute_value{0.0f, 0.0f};
    glm::vec2 vec2_relative_value{0.0f, 0.0f};
    glm::quat pose_orientation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 pose_position      {0.0f, 0.0f, 0.0f};
};

} // namespace erhe::application

