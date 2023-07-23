#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_log.hpp"
#include "erhe/toolkit/unique_id.hpp"

namespace erhe::primitive
{

auto Material::get_static_type()       -> uint64_t         { return erhe::Item_type::material;}
auto Material::get_type       () const -> uint64_t         { return get_static_type(); }
auto Material::get_type_name  () const -> std::string_view { return static_type_name; }

Material::Material()
    : erhe::Item{erhe::toolkit::Unique_id<Material>{}.get_id()}
{
}

Material::Material(
    const std::string_view name,
    const glm::vec3        base_color,
    const glm::vec2        roughness,
    const float            metallic
)
    : erhe::Item{name, erhe::toolkit::Unique_id<Material>{}.get_id()}
    , base_color{base_color, 1.0f}
    , roughness {roughness}
    , metallic  {metallic}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Material::~Material() = default;

} // namespace erhe::primitive
