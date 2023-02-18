#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace erhe::raytrace
{

class IGeometry;
class IInstance;

class Ray
{
public:
    auto transform(const glm::mat4& matrix) const -> Ray;

    glm::vec3 origin   {0.0f};
    float     t_near   {0.0f};
    glm::vec3 direction{0.0f};
    float     time     {0.0f};
    float     t_far    {0.0f};
    uint32_t  mask     {0xfffffffu};
    uint32_t  id       {0};
    uint32_t  flags    {0};
};

class Hit
{
public:
    glm::vec3    normal      {0.0f};
    glm::vec2    uv          {0.0f};
    unsigned int primitive_id{0};
    IGeometry*   geometry    {nullptr};
    IInstance*   instance    {nullptr};
};

} // namespace erhe::raytrace
