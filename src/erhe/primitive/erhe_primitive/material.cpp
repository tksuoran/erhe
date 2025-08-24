#include "erhe_primitive/material.hpp"

namespace erhe::primitive {

auto Material::get_static_type()       -> uint64_t         { return erhe::Item_type::material;}
auto Material::get_type       () const -> uint64_t         { return get_static_type(); }
auto Material::get_type_name  () const -> std::string_view { return static_type_name; }

Material::Material()                           = default;
Material::Material(const Material&)            = default;
Material& Material::operator=(const Material&) = default;
Material::~Material() noexcept                 = default;

Material::Material(const Material_create_info& create_info)
    : Item                      {create_info.name}
    , base_color                {create_info.base_color}
    , opacity                   {create_info.opacity}
    , roughness                 {create_info.roughness}
    , metallic                  {create_info.metallic}
    , reflectance               {create_info.reflectance}
    , emissive                  {create_info.emissive}
    , normal_texture_scale      {create_info.normal_texture_scale}
    , occlusion_texture_strength{create_info.occlusion_texture_strength}
    , unlit                     {create_info.unlit}
    , texture_samplers          {create_info.texture_samplers}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::primitive
