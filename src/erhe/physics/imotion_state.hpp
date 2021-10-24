#pragma once

#include <glm/glm.hpp>

namespace erhe::physics
{

class IMotion_state
{
public:
    virtual void get_world_transform(glm::mat3& basis, glm::vec3& origin)           = 0;
    virtual void set_world_transform(const glm::mat3 basis, const glm::vec3 origin) = 0;
};

} // namespace erhe::physics
