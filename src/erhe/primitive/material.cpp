#include "erhe/primitive/material.hpp"

namespace erhe::primitive
{

Material::Material()
{
}

Material::~Material() noexcept
{
}

Material::Material(
    const std::string_view name,
    const glm::vec3        base_color,
    const glm::vec2        roughness,
    const float            metallic
)
    : name      {name}
    , metallic  {metallic}
    , roughness {roughness}
    , base_color{base_color, 1.0f}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
    , visible   {true}
{
}

}
