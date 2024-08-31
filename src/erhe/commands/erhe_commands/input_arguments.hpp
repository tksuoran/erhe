#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdint>

namespace erhe::commands {

struct Input_arguments
{
    uint32_t modifier_mask;
    std::chrono::steady_clock::time_point timestamp;

    union Variant {
        bool button_pressed;

        float float_value;

        struct Vector2 {
            glm::vec2 absolute_value;
            glm::vec2 relative_value;
        } vector2;

        struct Pose {
            glm::quat orientation;
            glm::vec3 position;
        } pose;
    } variant;
};

} // namespace erhe::commands
