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
    : Item       {create_info.name}
    , base_color {create_info.base_color}
    , opacity    {create_info.opacity}
    , roughness  {create_info.roughness}
    , metallic   {create_info.metallic}
    , reflectance{create_info.reflectance}
    , emissive   {create_info.emissive}
    , textures{
        .base_color        {create_info.textures.base_color},
        .metallic_roughness{create_info.textures.metallic_roughness}
    }
    , samplers{
        .base_color        {create_info.samplers.base_color},
        .metallic_roughness{create_info.samplers.metallic_roughness}
    }
    , tex_coords{
        .base_color        {create_info.tex_coords.base_color},
        .metallic_roughness{create_info.tex_coords.metallic_roughness}
    }
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::primitive
