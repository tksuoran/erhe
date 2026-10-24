#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::xr {

// A rigid pose: orientation and position. Plain glm data with no OpenXR
// types, available in every ERHE_XR_LIBRARY configuration.
class Pose
{
public:
    glm::quat orientation{};
    glm::vec3 position{};
};

} // namespace erhe::xr
