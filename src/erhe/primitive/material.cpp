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

}