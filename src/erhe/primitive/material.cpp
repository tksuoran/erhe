#include "erhe/primitive/material.hpp"

namespace erhe::primitive
{

Material::Material() = default;

Material::~Material() = default;

Material::Material(
    std::string_view name,
    glm::vec4        base_color,
    float            roughness,
    float            anisotropy,
    float            metallic
)
    : name      {name}
    , metallic  {metallic}
    , roughness {roughness}
    , anisotropy{anisotropy}
    , base_color{base_color}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
    , visible   {false}
{
}

Material::Material(
    const std::string_view name,
    const glm::vec3        base_color
)
    : name      {name}
    , metallic  {0.99f}
    , roughness {0.15f}
    , anisotropy{0.0f}
    , base_color{glm::vec4{base_color, 1.0f}}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
    , visible   {true}
{
}


}
