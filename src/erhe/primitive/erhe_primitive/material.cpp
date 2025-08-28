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
    : Item{create_info.name}
    , data{create_info.data}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs)
{
    return
        (lhs.texture   == rhs.texture  ) &&
        (lhs.sampler   == rhs.sampler  ) &&
        (lhs.tex_coord == rhs.tex_coord) &&
        (lhs.rotation  == rhs.rotation ) &&
        (lhs.offset    == rhs.offset   ) &&
        (lhs.scale     == rhs.scale    );

}
[[nodiscard]] auto operator!=(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs)
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs)
{
    return
        (lhs.base_color         == rhs.base_color        ) &&
        (lhs.metallic_roughness == rhs.metallic_roughness) &&
        (lhs.normal             == rhs.normal            ) &&
        (lhs.occlusion          == rhs.occlusion         ) &&
        (lhs.emissive           == rhs.emissive          );
}

[[nodiscard]] auto operator!=(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs)
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return
        (lhs.base_color                 == rhs.base_color                ) &&
        (lhs.opacity                    == rhs.opacity                   ) &&
        (lhs.roughness                  == rhs.roughness                 ) &&
        (lhs.metallic                   == rhs.metallic                  ) &&
        (lhs.reflectance                == rhs.reflectance               ) &&
        (lhs.emissive                   == rhs.emissive                  ) &&
        (lhs.normal_texture_scale       == rhs.normal_texture_scale      ) &&
        (lhs.occlusion_texture_strength == rhs.occlusion_texture_strength) &&
        (lhs.unlit                      == rhs.unlit                     ) &&
        (lhs.texture_samplers           == rhs.texture_samplers          );
}

[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool
{
    return
        (lhs.get_name() == rhs.get_name()) &&
        (lhs.data       == rhs.data);
}

[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::primitive
