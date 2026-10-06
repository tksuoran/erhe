#include "operations/material_edit_operation.hpp"
#include "operations/property_edit_operation.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_property/property_set.hpp"

#include <fmt/format.h>

#include <array>
#include <vector>

namespace editor {

namespace {

using erhe::primitive::Material;
using erhe::primitive::Material_sampler_state;
using erhe::primitive::Material_texture_sampler;
using erhe::primitive::Material_texture_samplers;
using erhe::primitive::Texgen_mode;
using erhe::property::Property;

// The properties one texture slot's fields are stored in.
class Slot_properties
{
public:
    Material_texture_sampler Material_texture_samplers::*                 slot;
    const Property<erhe::property::Object_reference>&                     texture;
    const Property<Texgen_mode>&                                          texgen_mode;
    const Property<float>&                                                rotation;
    const Property<glm::vec2>&                                            offset;
    const Property<glm::vec2>&                                            scale;
    const Property<erhe::graphics::Sampler_address_mode>&                 wrap_u;
    const Property<erhe::graphics::Sampler_address_mode>&                 wrap_v;
    const Property<erhe::graphics::Filter>&                               min_filter;
    const Property<erhe::graphics::Filter>&                               mag_filter;
    const Property<erhe::graphics::Sampler_mipmap_mode>&                  mipmap_mode;
    const Property<float>&                                                max_anisotropy;
    const Property<float>&                                                lod_bias;
};

auto get_slot_properties() -> const std::array<Slot_properties, 5>&
{
    static const std::array<Slot_properties, 5> c_slots{
        Slot_properties{
            &Material_texture_samplers::base_color,
            Material::base_color_texture_property, Material::base_color_texture_texgen_mode_property,
            Material::base_color_texture_uv_rotation_property, Material::base_color_texture_uv_offset_property, Material::base_color_texture_uv_scale_property,
            Material::base_color_texture_wrap_u_property, Material::base_color_texture_wrap_v_property,
            Material::base_color_texture_min_filter_property, Material::base_color_texture_mag_filter_property, Material::base_color_texture_mipmap_mode_property,
            Material::base_color_texture_max_anisotropy_property, Material::base_color_texture_lod_bias_property
        },
        Slot_properties{
            &Material_texture_samplers::metallic_roughness,
            Material::metallic_roughness_texture_property, Material::metallic_roughness_texture_texgen_mode_property,
            Material::metallic_roughness_texture_uv_rotation_property, Material::metallic_roughness_texture_uv_offset_property, Material::metallic_roughness_texture_uv_scale_property,
            Material::metallic_roughness_texture_wrap_u_property, Material::metallic_roughness_texture_wrap_v_property,
            Material::metallic_roughness_texture_min_filter_property, Material::metallic_roughness_texture_mag_filter_property, Material::metallic_roughness_texture_mipmap_mode_property,
            Material::metallic_roughness_texture_max_anisotropy_property, Material::metallic_roughness_texture_lod_bias_property
        },
        Slot_properties{
            &Material_texture_samplers::normal,
            Material::normal_texture_property, Material::normal_texture_texgen_mode_property,
            Material::normal_texture_uv_rotation_property, Material::normal_texture_uv_offset_property, Material::normal_texture_uv_scale_property,
            Material::normal_texture_wrap_u_property, Material::normal_texture_wrap_v_property,
            Material::normal_texture_min_filter_property, Material::normal_texture_mag_filter_property, Material::normal_texture_mipmap_mode_property,
            Material::normal_texture_max_anisotropy_property, Material::normal_texture_lod_bias_property
        },
        Slot_properties{
            &Material_texture_samplers::occlusion,
            Material::occlusion_texture_property, Material::occlusion_texture_texgen_mode_property,
            Material::occlusion_texture_uv_rotation_property, Material::occlusion_texture_uv_offset_property, Material::occlusion_texture_uv_scale_property,
            Material::occlusion_texture_wrap_u_property, Material::occlusion_texture_wrap_v_property,
            Material::occlusion_texture_min_filter_property, Material::occlusion_texture_mag_filter_property, Material::occlusion_texture_mipmap_mode_property,
            Material::occlusion_texture_max_anisotropy_property, Material::occlusion_texture_lod_bias_property
        },
        Slot_properties{
            &Material_texture_samplers::emissive,
            Material::emissive_texture_property, Material::emissive_texture_texgen_mode_property,
            Material::emissive_texture_uv_rotation_property, Material::emissive_texture_uv_offset_property, Material::emissive_texture_uv_scale_property,
            Material::emissive_texture_wrap_u_property, Material::emissive_texture_wrap_v_property,
            Material::emissive_texture_min_filter_property, Material::emissive_texture_mag_filter_property, Material::emissive_texture_mipmap_mode_property,
            Material::emissive_texture_max_anisotropy_property, Material::emissive_texture_lod_bias_property
        }
    };
    return c_slots;
}

// One changed slot: which slot, and its fields after the edit.
class Slot_change
{
public:
    std::size_t              slot_index;
    Material_texture_sampler before;
    Material_texture_sampler after;
};

[[nodiscard]] auto is_same_slot(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs) -> bool
{
    return
        (lhs.texture_reference == rhs.texture_reference) &&
        (lhs.sampler           == rhs.sampler) &&
        (lhs.texgen_mode       == rhs.texgen_mode) &&
        (lhs.rotation          == rhs.rotation) &&
        (lhs.offset            == rhs.offset) &&
        (lhs.scale             == rhs.scale);
}

// Writes `after` as a local value when it differs from `before`: a changed
// field is an explicit request, so it becomes local even when it equals the
// default.
template <typename T>
void write_if_changed(Material& material, const Property<T>& property, const T& before, const T& after)
{
    if (before == after) {
        return;
    }
    material.set_value(property, after);
}

void write_slot_change(Material& material, const Slot_change& change)
{
    const Slot_properties&          properties = get_slot_properties()[change.slot_index];
    const Material_texture_sampler& before     = change.before;
    const Material_texture_sampler& after      = change.after;
    if (before.texture_reference != after.texture_reference) {
        if (after.texture_reference) {
            material.set_slot_texture(material.get_data().texture_samplers.*properties.slot, after.texture_reference);
        } else {
            material.clear_value(properties.texture);
        }
    }
    write_if_changed(material, properties.texgen_mode,         before.texgen_mode,            after.texgen_mode);
    write_if_changed(material, properties.rotation,            before.rotation,               after.rotation);
    write_if_changed(material, properties.offset,              before.offset,                 after.offset);
    write_if_changed(material, properties.scale,               before.scale,                  after.scale);
    const Material_sampler_state& sampler_before   = before.sampler;
    const Material_sampler_state& sampler_after    = after.sampler;
    write_if_changed(material, properties.wrap_u,              sampler_before.wrap_u,         sampler_after.wrap_u);
    write_if_changed(material, properties.wrap_v,              sampler_before.wrap_v,         sampler_after.wrap_v);
    write_if_changed(material, properties.min_filter,          sampler_before.min_filter,     sampler_after.min_filter);
    write_if_changed(material, properties.mag_filter,          sampler_before.mag_filter,     sampler_after.mag_filter);
    write_if_changed(material, properties.mipmap_mode,         sampler_before.mipmap_mode,    sampler_after.mipmap_mode);
    write_if_changed(material, properties.max_anisotropy,      sampler_before.max_anisotropy, sampler_after.max_anisotropy);
    write_if_changed(material, properties.lod_bias,            sampler_before.lod_bias,       sampler_after.lod_bias);
}

} // anonymous namespace

auto make_material_edit_operation(
    const std::shared_ptr<erhe::primitive::Material>& material,
    const erhe::primitive::Material_values&           before_values,
    const erhe::primitive::Material_values&           after_values,
    const erhe::primitive::Material_data&             before_data,
    const erhe::primitive::Material_data&             after_data
) -> std::shared_ptr<Property_edit_operation>
{
    if (!material) {
        return {};
    }
    erhe::property::Property_set values = erhe::property::Property_set::diff(
        Material::to_property_set(before_values),
        Material::to_property_set(after_values)
    );
    std::vector<Slot_change> slot_changes;
    const std::array<Slot_properties, 5>& slots = get_slot_properties();
    for (std::size_t i = 0, end = slots.size(); i < end; ++i) {
        const Material_texture_sampler& before = before_data.texture_samplers.*slots[i].slot;
        const Material_texture_sampler& after  = after_data.texture_samplers.*slots[i].slot;
        if (!is_same_slot(before, after)) {
            slot_changes.push_back(Slot_change{.slot_index = i, .before = before, .after = after});
        }
    }
    if (values.empty() && slot_changes.empty()) {
        return {};
    }
    return std::make_shared<Property_edit_operation>(
        fmt::format("Material change {}", material->get_name()),
        [material, values = std::move(values), slot_changes = std::move(slot_changes)]() {
            const erhe::property::Dependency_object::Change_batch batch{*material};
            for (const erhe::property::Property_set::Entry& entry : values.entries()) {
                material->set_value(*entry.property, entry.value);
            }
            for (const Slot_change& change : slot_changes) {
                write_slot_change(*material, change);
            }
        }
    );
}

}
