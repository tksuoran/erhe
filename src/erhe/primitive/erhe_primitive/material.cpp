#include "erhe_primitive/material.hpp"

namespace erhe::primitive {

auto Material::get_static_type()       -> uint64_t         { return erhe::Item_type::material;}
auto Material::get_type       () const -> uint64_t         { return get_static_type(); }
auto Material::get_type_name  () const -> std::string_view { return static_type_name; }

Material::Material()                           = default;
Material::Material(const Material&)            = default;
Material& Material::operator=(const Material&) = default;
Material::~Material() noexcept                 = default;

Material::Material(
    const std::string_view name,
    const glm::vec3        base_color,
    const glm::vec2        roughness,
    const float            metallic
)
    : Item      {name}
    , base_color{base_color, 1.0f}
    , roughness {roughness}
    , metallic  {metallic}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::primitive
