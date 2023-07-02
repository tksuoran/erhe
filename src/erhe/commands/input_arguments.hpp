#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::commands {

union Input_arguments
{
    bool button_pressed;

    float float_value;

    struct Vector2
    {
        glm::vec2 absolute_value;
        glm::vec2 relative_value;
    } vector2;

    struct Pose
    {
        glm::quat orientation;
        glm::vec3 position;
    } pose;
};

} // namespace erhe::commands/

