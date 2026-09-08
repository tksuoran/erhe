#include "erhe_primitive/material.hpp"

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/primitive_log.hpp"

#include <glm/gtc/constants.hpp>

namespace erhe::primitive {

namespace {

using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

const erhe::property::Owner_type c_owner = Material::property_owner_type();

auto unit_range(const Property_value& v) -> bool
{
    const float f = std::get<float>(v);
    return (f >= 0.0f) && (f <= 1.0f);
}

auto slider(
    const float                     min,
    const float                     max,
    const std::string_view          label,
    const std::string_view          tooltip      = {},
    const Property_ui::Visible_when visible_when = {}
) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .tooltip = tooltip, .label = label, .visible_when = visible_when};
}

// Flags: the draw lists partition by blending class, double-sidedness, the
// unlit shadow filter and the aniso control (Draw_list_scene, Draw_list_key);
// the shader variant key reads blending mode, BxDF model, normal map
// encoding and the brushed-metal block (Shader_key).
constexpr uint32_t c_partition = Property_flags::serialize | Property_flags::affects_draw_list_partition;
constexpr uint32_t c_variant   = Property_flags::serialize | Property_flags::affects_shader_variant;

// Property_flags::native_gltf: the exporter writes the value through a
// native glTF field or a typed ERHE_material field whenever it differs from
// the default (process_material / record_material_extensions). The fields
// whose carrier is conditional on something other than the value itself -
// alpha_cutoff (written only in the MASK alpha mode), normal_texture_scale
// and occlusion_texture_strength (only with that slot's texture bound),
// circular_brushed_metal_texgen_mode (only with the brushed metal block on)
// and reflectance (no glTF carrier at all) - are deliberately NOT flagged.
constexpr uint32_t c_native                  = Property_flags::serialize | Property_flags::native_gltf;
constexpr uint32_t c_partition_native        = c_partition | Property_flags::native_gltf;
constexpr uint32_t c_variant_native          = c_variant   | Property_flags::native_gltf;
constexpr uint32_t c_partition_variant_native = c_partition | c_variant | Property_flags::native_gltf;

// Row visibility (Property_ui::visible_when): the PBR rows hide for unlit
// materials, the anisotropy rows show only for BxDF models that support
// anisotropy, and alpha cutoff only in the alpha-test blending mode.
auto is_lit(const erhe::property::Dependency_object& object) -> bool
{
    return static_cast<const Material&>(object).get_bxdf_model() != Bxdf_model::unlit;
}
auto is_anisotropic(const erhe::property::Dependency_object& object) -> bool
{
    return supports_anisotropy(static_cast<const Material&>(object).get_bxdf_model());
}
auto is_brushed_metal(const erhe::property::Dependency_object& object) -> bool
{
    const Material& material = static_cast<const Material&>(object);
    return supports_anisotropy(material.get_bxdf_model()) && material.get_use_circular_brushed_metal();
}
auto is_alpha_test(const erhe::property::Dependency_object& object) -> bool
{
    return static_cast<const Material&>(object).get_blending_mode() == Material_blending_mode::alpha_test;
}

} // anonymous namespace

const Property<glm::vec3> Material::base_color_property = Property<glm::vec3>::register_property(
    "base_color", c_owner, Property_metadata{.default_value = glm::vec3{1.0f, 1.0f, 1.0f}, .inherits = true, .flags = c_native, .ui = Property_ui{.presentation = Property_ui::Presentation::color, .label = "Base Color"}}
);
const Property<float> Material::opacity_property = Property<float>::register_property(
    "opacity", c_owner, Property_metadata{.default_value = 1.0f, .inherits = true, .flags = c_native, .ui = slider(0.0f, 1.0f, "Opacity")}, unit_range
);
const Property<glm::vec2> Material::roughness_property = Property<glm::vec2>::register_property(
    "roughness", c_owner, Property_metadata{.default_value = glm::vec2{0.5f, 0.5f}, .inherits = true, .flags = c_native, .ui = Property_ui{.min = 0.001f, .max = 1.0f, .step = 0.005f, .tooltip = "X and Y roughness; Y is used by anisotropic BxDF models", .label = "Roughness", .visible_when = is_lit}}
);
const Property<float> Material::metallic_property = Property<float>::register_property(
    "metallic", c_owner, Property_metadata{.default_value = 0.0f, .inherits = true, .flags = c_native, .ui = slider(0.0f, 1.0f, "Metallic", {}, is_lit)}, unit_range
);
const Property<float> Material::reflectance_property = Property<float>::register_property(
    "reflectance", c_owner, Property_metadata{.default_value = 0.5f, .inherits = true, .ui = slider(0.35f, 1.0f, "Reflectance", {}, is_lit)}
);
const Property<glm::vec3> Material::emissive_property = Property<glm::vec3>::register_property(
    "emissive", c_owner, Property_metadata{.default_value = glm::vec3{0.0f, 0.0f, 0.0f}, .inherits = true, .flags = c_native, .ui = Property_ui{.presentation = Property_ui::Presentation::color, .label = "Emissive"}}
);
const Property<float> Material::ior_property = Property<float>::register_property(
    "ior", c_owner, Property_metadata{.default_value = 1.5f, .inherits = true, .flags = c_native, .ui = slider(1.0f, 3.0f, "IOR", "Index of refraction", is_lit)}
);
const Property<float> Material::transmission_property = Property<float>::register_property(
    "transmission", c_owner, Property_metadata{.default_value = 0.0f, .inherits = true, .flags = c_native, .ui = slider(0.0f, 1.0f, "Transmission", {}, is_lit)}, unit_range
);
const Property<float> Material::normal_texture_scale_property = Property<float>::register_property(
    "normal_texture_scale", c_owner, Property_metadata{.default_value = 1.0f, .inherits = true, .ui = slider(0.0f, 1.0f, "Normal Map Scale", "Strength of the bound normal texture")}
);
// The decode of a bound normal texture: `texel * scale + bias`. The
// defaults are the UsdPreviewSurface ones and glTF's fixed mapping, so a
// material that authors neither decodes a 0..1 map into -1..1. glTF has no
// field for either, so neither is flagged native_gltf; USD writes them as
// the normal slot's UsdUVTexture inputs:scale and inputs:bias.
const Property<glm::vec4> Material::normal_texture_decode_scale_property = Property<glm::vec4>::register_property(
    "normal_texture_decode_scale", c_owner,
    Property_metadata{
        .default_value = glm::vec4{2.0f, 2.0f, 2.0f, 2.0f},
        .inherits      = true,
        .ui            = Property_ui{.min = -4.0f, .max = 4.0f, .step = 0.01f, .tooltip = "Per-channel scale applied to the normal texture's texels before the bias", .label = "Normal Map Decode Scale"}
    }
);
const Property<glm::vec4> Material::normal_texture_decode_bias_property = Property<glm::vec4>::register_property(
    "normal_texture_decode_bias", c_owner,
    Property_metadata{
        .default_value = glm::vec4{-1.0f, -1.0f, -1.0f, -1.0f},
        .inherits      = true,
        .ui            = Property_ui{.min = -4.0f, .max = 4.0f, .step = 0.01f, .tooltip = "Per-channel bias added to the scaled normal texture texels", .label = "Normal Map Decode Bias"}
    }
);
const Property<Normalmap_encoding> Material::normalmap_encoding_property = Property<Normalmap_encoding>::register_property(
    "normalmap_encoding", c_owner, c_normalmap_encoding_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Normalmap_encoding::right_handed_three_channel),
        .inherits      = true,
        .flags         = c_variant_native,
        .ui            = Property_ui{.tooltip = "Storage encoding of the bound normal texture. A KTX2 normal-mode texture overrides the channel layout; the handedness is always honored", .label = "Normal Map Encoding"}
    }
);
const Property<float> Material::occlusion_texture_strength_property = Property<float>::register_property(
    "occlusion_texture_strength", c_owner, Property_metadata{.default_value = 1.0f, .inherits = true, .ui = slider(0.0f, 1.0f, "Occlusion Strength")}, unit_range
);
// Which channel of the slot's texture each scalar input reads. The visible
// rows follow the slot that supplies the channel, so a material with no
// texture in that slot does not offer a channel to pick.
const Property<Texture_channel> Material::metallic_channel_property = Property<Texture_channel>::register_property(
    "metallic_channel", c_owner, c_texture_channel_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texture_channel::b), .inherits = true, .ui = Property_ui{.tooltip = "Channel of the metallic roughness texture the metallic factor multiplies", .label = "Metallic Channel", .visible_when = is_lit}}
);
const Property<Texture_channel> Material::roughness_channel_property = Property<Texture_channel>::register_property(
    "roughness_channel", c_owner, c_texture_channel_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texture_channel::g), .inherits = true, .ui = Property_ui{.tooltip = "Channel of the metallic roughness texture the roughness factor multiplies", .label = "Roughness Channel", .visible_when = is_lit}}
);
const Property<Texture_channel> Material::occlusion_channel_property = Property<Texture_channel>::register_property(
    "occlusion_channel", c_owner, c_texture_channel_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texture_channel::r), .inherits = true, .ui = Property_ui{.tooltip = "Channel of the occlusion texture the occlusion strength applies to", .label = "Occlusion Channel", .visible_when = is_lit}}
);
const Property<Texture_channel> Material::opacity_channel_property = Property<Texture_channel>::register_property(
    "opacity_channel", c_owner, c_texture_channel_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texture_channel::a), .inherits = true, .ui = Property_ui{.tooltip = "Channel of the base color texture the fragment alpha comes from", .label = "Opacity Channel"}}
);
const Property<Bxdf_model> Material::bxdf_model_property = Property<Bxdf_model>::register_property(
    "bxdf_model", c_owner, c_bxdf_model_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Bxdf_model::isotropic_brdf), .inherits = true, .flags = c_partition_variant_native, .ui = Property_ui{.label = "BxDF Model"}}
);
const Property<Material_blending_mode> Material::blending_mode_property = Property<Material_blending_mode>::register_property(
    "blending_mode", c_owner, c_material_blending_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Material_blending_mode::opaque), .inherits = true, .flags = c_partition_variant_native, .ui = Property_ui{.label = "Blending Mode"}}
);
const Property<bool> Material::double_sided_property = Property<bool>::register_property(
    "double_sided", c_owner, Property_metadata{.default_value = false, .inherits = true, .flags = c_partition_native, .ui = Property_ui{.label = "Double Sided"}}
);
const Property<float> Material::alpha_cutoff_property = Property<float>::register_property(
    "alpha_cutoff", c_owner, Property_metadata{.default_value = 0.5f, .inherits = true, .ui = slider(0.0f, 1.0f, "Alpha Cutoff", "Used by the Alpha Test blending mode", is_alpha_test)}, unit_range
);
const Property<bool> Material::use_circular_brushed_metal_property = Property<bool>::register_property(
    "use_circular_brushed_metal", c_owner, Property_metadata{.default_value = false, .inherits = true, .flags = c_variant_native, .ui = Property_ui{.tooltip = "Anisotropic BxDF models only", .label = "Circular Brushed Metal", .visible_when = is_anisotropic}}
);
const Property<Texgen_mode> Material::circular_brushed_metal_texgen_mode_property = Property<Texgen_mode>::register_property(
    "circular_brushed_metal_texgen_mode", c_owner, c_texgen_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texgen_mode::uv1), .inherits = true, .flags = c_variant, .ui = Property_ui{.tooltip = "Texgen source for the circular brushed metal block", .label = "Brushed Metal Texgen", .visible_when = is_brushed_metal}}
);
const Property<bool> Material::use_aniso_control_property = Property<bool>::register_property(
    "use_aniso_control", c_owner, Property_metadata{.default_value = false, .inherits = true, .flags = c_partition_variant_native, .ui = Property_ui{.tooltip = "Anisotropic BxDF models only", .label = "Aniso Control", .visible_when = is_anisotropic}}
);

// Texture slots (D28): entry-store object references that inherit (a
// content-library folder can hold them for the materials below it,
// doc/property-system.md D30); Material::on_property_changed mirrors the
// effective value into the Material_data slot the per-frame readers use.
// A bound slot selects the texture-using shader variant and a normal
// texture's two-component flag rides the texture (Shader_key), so a change
// is a shader variant change.
using Texture_slot = std::shared_ptr<erhe::graphics::Texture_reference>;
using Slot_traits  = erhe::property::Member_value_traits<Texture_slot>;
constexpr uint64_t c_texture_types = erhe::Item_type::texture | erhe::Item_type::graph_texture;

auto texture_ui(const std::string_view label, const Property_ui::Visible_when visible_when = {}) -> Property_ui
{
    return Property_ui{.group = "Textures", .label = label, .visible_when = visible_when, .reference_item_types = c_texture_types};
}

const Property<Object_reference> Material::base_color_texture_property = Property<Object_reference>::register_property(
    "base_color_texture", c_owner, Property_metadata{.inherits = true, .flags = c_variant_native, .ui = texture_ui("Base Color Texture")}, Slot_traits::validate
);
const Property<Object_reference> Material::metallic_roughness_texture_property = Property<Object_reference>::register_property(
    "metallic_roughness_texture", c_owner, Property_metadata{.inherits = true, .flags = c_variant_native, .ui = texture_ui("Metallic Roughness Texture", is_lit)}, Slot_traits::validate
);
const Property<Object_reference> Material::normal_texture_property = Property<Object_reference>::register_property(
    "normal_texture", c_owner, Property_metadata{.inherits = true, .flags = c_variant_native, .ui = texture_ui("Normal Texture", is_lit)}, Slot_traits::validate
);
const Property<Object_reference> Material::occlusion_texture_property = Property<Object_reference>::register_property(
    "occlusion_texture", c_owner, Property_metadata{.inherits = true, .flags = c_variant_native, .ui = texture_ui("Occlusion Texture", is_lit)}, Slot_traits::validate
);
const Property<Object_reference> Material::emissive_texture_property = Property<Object_reference>::register_property(
    "emissive_texture", c_owner, Property_metadata{.inherits = true, .flags = c_variant_native, .ui = texture_ui("Emissive Texture")}, Slot_traits::validate
);

namespace {

// Slot transforms: entry-store properties that inherit like the slot
// texture, mirrored into the same slot's texgen_mode, rotation, offset and
// scale by Material::on_property_changed; the rows show while the slot is
// bound (the PBR slots also only while lit, as the slot texture itself).
// The texgen source selects a shader variant (Shader_key), the UV
// transform is a uniform.
using Slot_pointer = Material_texture_sampler Material_texture_samplers::*;

enum class Slot_lighting : unsigned int { any = 0, lit = 1 };

auto slot_visible(const Slot_pointer slot, const Slot_lighting lighting) -> Property_ui::Visible_when
{
    return [slot, lighting](const erhe::property::Dependency_object& object) -> bool {
        const Material& material = static_cast<const Material&>(object);
        const bool bound = static_cast<bool>((material.data.texture_samplers.*slot).texture_reference);
        return bound && ((lighting == Slot_lighting::any) || is_lit(object));
    };
}

auto slot_texgen(const std::string_view name, const Slot_pointer slot, const std::string_view group, const Slot_lighting lighting) -> Property<Texgen_mode>
{
    return Property<Texgen_mode>::register_property(
        name, c_owner, c_texgen_mode_enum_info,
        Property_metadata{
            .default_value = erhe::property::make_value(Texgen_mode::uv0),
            .inherits      = true,
            .flags         = c_variant,
            .ui            = Property_ui{.group = group, .tooltip = "Texture coordinate source of the slot", .label = "Texgen", .visible_when = slot_visible(slot, lighting)}
        }
    );
}

auto slot_rotation(const std::string_view name, const Slot_pointer slot, const std::string_view group, const Slot_lighting lighting) -> Property<float>
{
    return Property<float>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = 0.0f,
            .inherits      = true,
            .ui            = Property_ui{.min = -glm::two_pi<float>(), .max = glm::two_pi<float>(), .presentation = Property_ui::Presentation::angle_degrees, .group = group, .label = "UV Rotation", .visible_when = slot_visible(slot, lighting)}
        }
    );
}

auto slot_offset(const std::string_view name, const Slot_pointer slot, const std::string_view group, const Slot_lighting lighting) -> Property<glm::vec2>
{
    return Property<glm::vec2>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = glm::vec2{0.0f, 0.0f},
            .inherits      = true,
            .ui            = Property_ui{.min = -10.0f, .max = 10.0f, .step = 0.01f, .group = group, .label = "UV Offset", .visible_when = slot_visible(slot, lighting)}
        }
    );
}

auto slot_scale(const std::string_view name, const Slot_pointer slot, const std::string_view group, const Slot_lighting lighting) -> Property<glm::vec2>
{
    return Property<glm::vec2>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = glm::vec2{1.0f, 1.0f},
            .inherits      = true,
            .ui            = Property_ui{.min = -10.0f, .max = 10.0f, .step = 0.01f, .group = group, .label = "UV Scale", .visible_when = slot_visible(slot, lighting)}
        }
    );
}

// Slot samplers: entry-store properties like the slot transforms, mirrored
// into the slot's Material_sampler_state; the rows sit under a
// "<Slot> Sampler" group while the slot is bound. Defaults are those of
// Material_sampler_state (= Sampler_create_info).
template <typename E>
auto slot_sampler_enum(
    const std::string_view                name,
    const Slot_pointer                    slot,
    const std::string_view                group,
    const Slot_lighting                   lighting,
    const erhe::property::Enum_info&      enum_info,
    const E                               default_value,
    const std::string_view                label
) -> Property<E>
{
    return Property<E>::register_property(
        name, c_owner, enum_info,
        Property_metadata{
            .default_value = erhe::property::Enum_value{static_cast<int32_t>(default_value)},
            .inherits      = true,
            .ui            = Property_ui{.group = group, .label = label, .visible_when = slot_visible(slot, lighting)}
        }
    );
}

auto slot_sampler_float(
    const std::string_view name,
    const Slot_pointer     slot,
    const std::string_view group,
    const Slot_lighting    lighting,
    const float            default_value,
    const float            min,
    const float            max,
    const std::string_view label,
    const bool             developer_only
) -> Property<float>
{
    return Property<float>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = default_value,
            .inherits      = true,
            .ui            = Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .group = group, .developer_only = developer_only, .label = label, .visible_when = slot_visible(slot, lighting)}
        }
    );
}

} // anonymous namespace

const Property<Texgen_mode> Material::base_color_texture_texgen_mode_property        = slot_texgen  ("base_color_texture_texgen_mode",           &Material_texture_samplers::base_color,             "Base Color Texture",          Slot_lighting::any);
const Property<float>       Material::base_color_texture_uv_rotation_property           = slot_rotation("base_color_texture_uv_rotation",              &Material_texture_samplers::base_color,             "Base Color Texture",          Slot_lighting::any);
const Property<glm::vec2>   Material::base_color_texture_uv_offset_property             = slot_offset  ("base_color_texture_uv_offset",                &Material_texture_samplers::base_color,             "Base Color Texture",          Slot_lighting::any);
const Property<glm::vec2>   Material::base_color_texture_uv_scale_property              = slot_scale   ("base_color_texture_uv_scale",                 &Material_texture_samplers::base_color,             "Base Color Texture",          Slot_lighting::any);
const Property<Texgen_mode> Material::metallic_roughness_texture_texgen_mode_property = slot_texgen  ("metallic_roughness_texture_texgen_mode",   &Material_texture_samplers::metallic_roughness,     "Metallic Roughness Texture",  Slot_lighting::lit);
const Property<float>       Material::metallic_roughness_texture_uv_rotation_property   = slot_rotation("metallic_roughness_texture_uv_rotation",      &Material_texture_samplers::metallic_roughness,     "Metallic Roughness Texture",  Slot_lighting::lit);
const Property<glm::vec2>   Material::metallic_roughness_texture_uv_offset_property     = slot_offset  ("metallic_roughness_texture_uv_offset",        &Material_texture_samplers::metallic_roughness,     "Metallic Roughness Texture",  Slot_lighting::lit);
const Property<glm::vec2>   Material::metallic_roughness_texture_uv_scale_property      = slot_scale   ("metallic_roughness_texture_uv_scale",         &Material_texture_samplers::metallic_roughness,     "Metallic Roughness Texture",  Slot_lighting::lit);
const Property<Texgen_mode> Material::normal_texture_texgen_mode_property            = slot_texgen  ("normal_texture_texgen_mode",               &Material_texture_samplers::normal,                 "Normal Texture",              Slot_lighting::lit);
const Property<float>       Material::normal_texture_uv_rotation_property               = slot_rotation("normal_texture_uv_rotation",                  &Material_texture_samplers::normal,                 "Normal Texture",              Slot_lighting::lit);
const Property<glm::vec2>   Material::normal_texture_uv_offset_property                 = slot_offset  ("normal_texture_uv_offset",                    &Material_texture_samplers::normal,                 "Normal Texture",              Slot_lighting::lit);
const Property<glm::vec2>   Material::normal_texture_uv_scale_property                  = slot_scale   ("normal_texture_uv_scale",                     &Material_texture_samplers::normal,                 "Normal Texture",              Slot_lighting::lit);
const Property<Texgen_mode> Material::occlusion_texture_texgen_mode_property         = slot_texgen  ("occlusion_texture_texgen_mode",            &Material_texture_samplers::occlusion,              "Occlusion Texture",           Slot_lighting::lit);
const Property<float>       Material::occlusion_texture_uv_rotation_property            = slot_rotation("occlusion_texture_uv_rotation",               &Material_texture_samplers::occlusion,              "Occlusion Texture",           Slot_lighting::lit);
const Property<glm::vec2>   Material::occlusion_texture_uv_offset_property              = slot_offset  ("occlusion_texture_uv_offset",                 &Material_texture_samplers::occlusion,              "Occlusion Texture",           Slot_lighting::lit);
const Property<glm::vec2>   Material::occlusion_texture_uv_scale_property               = slot_scale   ("occlusion_texture_uv_scale",                  &Material_texture_samplers::occlusion,              "Occlusion Texture",           Slot_lighting::lit);
const Property<Texgen_mode> Material::emissive_texture_texgen_mode_property          = slot_texgen  ("emissive_texture_texgen_mode",             &Material_texture_samplers::emissive,               "Emissive Texture",            Slot_lighting::any);
const Property<float>       Material::emissive_texture_uv_rotation_property             = slot_rotation("emissive_texture_uv_rotation",                &Material_texture_samplers::emissive,               "Emissive Texture",            Slot_lighting::any);
const Property<glm::vec2>   Material::emissive_texture_uv_offset_property               = slot_offset  ("emissive_texture_uv_offset",                  &Material_texture_samplers::emissive,               "Emissive Texture",            Slot_lighting::any);
const Property<glm::vec2>   Material::emissive_texture_uv_scale_property                = slot_scale   ("emissive_texture_uv_scale",                   &Material_texture_samplers::emissive,               "Emissive Texture",            Slot_lighting::any);
const Property<erhe::graphics::Sampler_address_mode> Material::base_color_texture_wrap_u_property         = slot_sampler_enum ("base_color_texture_wrap_u",         &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_u,      "Wrap U");
const Property<erhe::graphics::Sampler_address_mode> Material::base_color_texture_wrap_v_property         = slot_sampler_enum ("base_color_texture_wrap_v",         &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_v,      "Wrap V");
const Property<erhe::graphics::Filter>               Material::base_color_texture_min_filter_property     = slot_sampler_enum ("base_color_texture_min_filter",     &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, c_filter_enum_info,               Material_sampler_state{}.min_filter,  "Min Filter");
const Property<erhe::graphics::Filter>               Material::base_color_texture_mag_filter_property     = slot_sampler_enum ("base_color_texture_mag_filter",     &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, c_filter_enum_info,               Material_sampler_state{}.mag_filter,  "Mag Filter");
const Property<erhe::graphics::Sampler_mipmap_mode>  Material::base_color_texture_mipmap_mode_property    = slot_sampler_enum ("base_color_texture_mipmap_mode",    &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, c_sampler_mipmap_mode_enum_info,  Material_sampler_state{}.mipmap_mode, "Mipmap Mode");
const Property<float>                                Material::base_color_texture_max_anisotropy_property = slot_sampler_float("base_color_texture_max_anisotropy", &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, Material_sampler_state{}.max_anisotropy, 1.0f, 16.0f, "Max Anisotropy", false);
const Property<float>                                Material::base_color_texture_lod_bias_property       = slot_sampler_float("base_color_texture_lod_bias",       &Material_texture_samplers::base_color, "Base Color Sampler", Slot_lighting::any, Material_sampler_state{}.lod_bias, -4.0f, 4.0f, "LOD Bias", true);
const Property<erhe::graphics::Sampler_address_mode> Material::metallic_roughness_texture_wrap_u_property         = slot_sampler_enum ("metallic_roughness_texture_wrap_u",         &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_u,      "Wrap U");
const Property<erhe::graphics::Sampler_address_mode> Material::metallic_roughness_texture_wrap_v_property         = slot_sampler_enum ("metallic_roughness_texture_wrap_v",         &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_v,      "Wrap V");
const Property<erhe::graphics::Filter>               Material::metallic_roughness_texture_min_filter_property     = slot_sampler_enum ("metallic_roughness_texture_min_filter",     &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.min_filter,  "Min Filter");
const Property<erhe::graphics::Filter>               Material::metallic_roughness_texture_mag_filter_property     = slot_sampler_enum ("metallic_roughness_texture_mag_filter",     &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.mag_filter,  "Mag Filter");
const Property<erhe::graphics::Sampler_mipmap_mode>  Material::metallic_roughness_texture_mipmap_mode_property    = slot_sampler_enum ("metallic_roughness_texture_mipmap_mode",    &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, c_sampler_mipmap_mode_enum_info,  Material_sampler_state{}.mipmap_mode, "Mipmap Mode");
const Property<float>                                Material::metallic_roughness_texture_max_anisotropy_property = slot_sampler_float("metallic_roughness_texture_max_anisotropy", &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, Material_sampler_state{}.max_anisotropy, 1.0f, 16.0f, "Max Anisotropy", false);
const Property<float>                                Material::metallic_roughness_texture_lod_bias_property       = slot_sampler_float("metallic_roughness_texture_lod_bias",       &Material_texture_samplers::metallic_roughness, "Metallic Roughness Sampler", Slot_lighting::lit, Material_sampler_state{}.lod_bias, -4.0f, 4.0f, "LOD Bias", true);
const Property<erhe::graphics::Sampler_address_mode> Material::normal_texture_wrap_u_property         = slot_sampler_enum ("normal_texture_wrap_u",         &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_u,      "Wrap U");
const Property<erhe::graphics::Sampler_address_mode> Material::normal_texture_wrap_v_property         = slot_sampler_enum ("normal_texture_wrap_v",         &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_v,      "Wrap V");
const Property<erhe::graphics::Filter>               Material::normal_texture_min_filter_property     = slot_sampler_enum ("normal_texture_min_filter",     &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.min_filter,  "Min Filter");
const Property<erhe::graphics::Filter>               Material::normal_texture_mag_filter_property     = slot_sampler_enum ("normal_texture_mag_filter",     &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.mag_filter,  "Mag Filter");
const Property<erhe::graphics::Sampler_mipmap_mode>  Material::normal_texture_mipmap_mode_property    = slot_sampler_enum ("normal_texture_mipmap_mode",    &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, c_sampler_mipmap_mode_enum_info,  Material_sampler_state{}.mipmap_mode, "Mipmap Mode");
const Property<float>                                Material::normal_texture_max_anisotropy_property = slot_sampler_float("normal_texture_max_anisotropy", &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, Material_sampler_state{}.max_anisotropy, 1.0f, 16.0f, "Max Anisotropy", false);
const Property<float>                                Material::normal_texture_lod_bias_property       = slot_sampler_float("normal_texture_lod_bias",       &Material_texture_samplers::normal, "Normal Sampler", Slot_lighting::lit, Material_sampler_state{}.lod_bias, -4.0f, 4.0f, "LOD Bias", true);
const Property<erhe::graphics::Sampler_address_mode> Material::occlusion_texture_wrap_u_property         = slot_sampler_enum ("occlusion_texture_wrap_u",         &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_u,      "Wrap U");
const Property<erhe::graphics::Sampler_address_mode> Material::occlusion_texture_wrap_v_property         = slot_sampler_enum ("occlusion_texture_wrap_v",         &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_v,      "Wrap V");
const Property<erhe::graphics::Filter>               Material::occlusion_texture_min_filter_property     = slot_sampler_enum ("occlusion_texture_min_filter",     &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.min_filter,  "Min Filter");
const Property<erhe::graphics::Filter>               Material::occlusion_texture_mag_filter_property     = slot_sampler_enum ("occlusion_texture_mag_filter",     &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, c_filter_enum_info,               Material_sampler_state{}.mag_filter,  "Mag Filter");
const Property<erhe::graphics::Sampler_mipmap_mode>  Material::occlusion_texture_mipmap_mode_property    = slot_sampler_enum ("occlusion_texture_mipmap_mode",    &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, c_sampler_mipmap_mode_enum_info,  Material_sampler_state{}.mipmap_mode, "Mipmap Mode");
const Property<float>                                Material::occlusion_texture_max_anisotropy_property = slot_sampler_float("occlusion_texture_max_anisotropy", &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, Material_sampler_state{}.max_anisotropy, 1.0f, 16.0f, "Max Anisotropy", false);
const Property<float>                                Material::occlusion_texture_lod_bias_property       = slot_sampler_float("occlusion_texture_lod_bias",       &Material_texture_samplers::occlusion, "Occlusion Sampler", Slot_lighting::lit, Material_sampler_state{}.lod_bias, -4.0f, 4.0f, "LOD Bias", true);
const Property<erhe::graphics::Sampler_address_mode> Material::emissive_texture_wrap_u_property         = slot_sampler_enum ("emissive_texture_wrap_u",         &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_u,      "Wrap U");
const Property<erhe::graphics::Sampler_address_mode> Material::emissive_texture_wrap_v_property         = slot_sampler_enum ("emissive_texture_wrap_v",         &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, c_sampler_address_mode_enum_info, Material_sampler_state{}.wrap_v,      "Wrap V");
const Property<erhe::graphics::Filter>               Material::emissive_texture_min_filter_property     = slot_sampler_enum ("emissive_texture_min_filter",     &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, c_filter_enum_info,               Material_sampler_state{}.min_filter,  "Min Filter");
const Property<erhe::graphics::Filter>               Material::emissive_texture_mag_filter_property     = slot_sampler_enum ("emissive_texture_mag_filter",     &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, c_filter_enum_info,               Material_sampler_state{}.mag_filter,  "Mag Filter");
const Property<erhe::graphics::Sampler_mipmap_mode>  Material::emissive_texture_mipmap_mode_property    = slot_sampler_enum ("emissive_texture_mipmap_mode",    &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, c_sampler_mipmap_mode_enum_info,  Material_sampler_state{}.mipmap_mode, "Mipmap Mode");
const Property<float>                                Material::emissive_texture_max_anisotropy_property = slot_sampler_float("emissive_texture_max_anisotropy", &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, Material_sampler_state{}.max_anisotropy, 1.0f, 16.0f, "Max Anisotropy", false);
const Property<float>                                Material::emissive_texture_lod_bias_property       = slot_sampler_float("emissive_texture_lod_bias",       &Material_texture_samplers::emissive, "Emissive Sampler", Slot_lighting::any, Material_sampler_state{}.lod_bias, -4.0f, 4.0f, "LOD Bias", true);

namespace {

// The seven sampler properties of one slot, for the mirror, the seed and
// set_data.
class Slot_sampler_properties
{
public:
    const Property<erhe::graphics::Sampler_address_mode>& wrap_u;
    const Property<erhe::graphics::Sampler_address_mode>& wrap_v;
    const Property<erhe::graphics::Filter>&               min_filter;
    const Property<erhe::graphics::Filter>&               mag_filter;
    const Property<erhe::graphics::Sampler_mipmap_mode>&  mipmap_mode;
    const Property<float>&                                max_anisotropy;
    const Property<float>&                                lod_bias;
};

auto sampler_properties_of(const Material_texture_samplers& samplers, const Material_texture_sampler& slot) -> const Slot_sampler_properties*
{
    static const Slot_sampler_properties c_base_color        {Material::base_color_texture_wrap_u_property,         Material::base_color_texture_wrap_v_property,         Material::base_color_texture_min_filter_property,         Material::base_color_texture_mag_filter_property,         Material::base_color_texture_mipmap_mode_property,         Material::base_color_texture_max_anisotropy_property,         Material::base_color_texture_lod_bias_property};
    static const Slot_sampler_properties c_metallic_roughness{Material::metallic_roughness_texture_wrap_u_property, Material::metallic_roughness_texture_wrap_v_property, Material::metallic_roughness_texture_min_filter_property, Material::metallic_roughness_texture_mag_filter_property, Material::metallic_roughness_texture_mipmap_mode_property, Material::metallic_roughness_texture_max_anisotropy_property, Material::metallic_roughness_texture_lod_bias_property};
    static const Slot_sampler_properties c_normal            {Material::normal_texture_wrap_u_property,             Material::normal_texture_wrap_v_property,             Material::normal_texture_min_filter_property,             Material::normal_texture_mag_filter_property,             Material::normal_texture_mipmap_mode_property,             Material::normal_texture_max_anisotropy_property,             Material::normal_texture_lod_bias_property};
    static const Slot_sampler_properties c_occlusion         {Material::occlusion_texture_wrap_u_property,          Material::occlusion_texture_wrap_v_property,          Material::occlusion_texture_min_filter_property,          Material::occlusion_texture_mag_filter_property,          Material::occlusion_texture_mipmap_mode_property,          Material::occlusion_texture_max_anisotropy_property,          Material::occlusion_texture_lod_bias_property};
    static const Slot_sampler_properties c_emissive          {Material::emissive_texture_wrap_u_property,           Material::emissive_texture_wrap_v_property,           Material::emissive_texture_min_filter_property,           Material::emissive_texture_mag_filter_property,           Material::emissive_texture_mipmap_mode_property,           Material::emissive_texture_max_anisotropy_property,           Material::emissive_texture_lod_bias_property};
    if (&slot == &samplers.base_color)         { return &c_base_color; }
    if (&slot == &samplers.metallic_roughness) { return &c_metallic_roughness; }
    if (&slot == &samplers.normal)             { return &c_normal; }
    if (&slot == &samplers.occlusion)          { return &c_occlusion; }
    if (&slot == &samplers.emissive)           { return &c_emissive; }
    return nullptr;
}

// Writes `state` through the slot's properties: a field at its default
// clears the local value (so a folder value shows through), any other
// value becomes local.
void apply_sampler_state(Material& material, const Slot_sampler_properties& properties, const Material_sampler_state& state)
{
    const Material_sampler_state defaults{};
    const auto apply = [&material]<typename T>(const Property<T>& property, const T value, const T default_value) {
        if (value != default_value) {
            material.set_value(property, value);
        } else {
            material.clear_value(property);
        }
    };
    apply(properties.wrap_u,         state.wrap_u,         defaults.wrap_u);
    apply(properties.wrap_v,         state.wrap_v,         defaults.wrap_v);
    apply(properties.min_filter,     state.min_filter,     defaults.min_filter);
    apply(properties.mag_filter,     state.mag_filter,     defaults.mag_filter);
    apply(properties.mipmap_mode,    state.mipmap_mode,    defaults.mipmap_mode);
    apply(properties.max_anisotropy, state.max_anisotropy, defaults.max_anisotropy);
    apply(properties.lod_bias,       state.lod_bias,       defaults.lod_bias);
}

// Mirrors the changed sampler property into the slot state; false when
// `changed` is not one of the slot's sampler properties.
auto mirror_sampler_state(const Material& material, const Slot_sampler_properties& properties, const erhe::property::Dependency_property* changed, Material_sampler_state& state) -> bool
{
    if (changed == properties.wrap_u.get_ptr())         { state.wrap_u         = material.get_value(properties.wrap_u);         return true; }
    if (changed == properties.wrap_v.get_ptr())         { state.wrap_v         = material.get_value(properties.wrap_v);         return true; }
    if (changed == properties.min_filter.get_ptr())     { state.min_filter     = material.get_value(properties.min_filter);     return true; }
    if (changed == properties.mag_filter.get_ptr())     { state.mag_filter     = material.get_value(properties.mag_filter);     return true; }
    if (changed == properties.mipmap_mode.get_ptr())    { state.mipmap_mode    = material.get_value(properties.mipmap_mode);    return true; }
    if (changed == properties.max_anisotropy.get_ptr()) { state.max_anisotropy = material.get_value(properties.max_anisotropy); return true; }
    if (changed == properties.lod_bias.get_ptr())       { state.lod_bias       = material.get_value(properties.lod_bias);       return true; }
    return false;
}

} // anonymous namespace

auto to_sampler_create_info(const Material_sampler_state& state) -> erhe::graphics::Sampler_create_info
{
    return erhe::graphics::Sampler_create_info{
        .min_filter     = state.min_filter,
        .mag_filter     = state.mag_filter,
        .mipmap_mode    = state.mipmap_mode,
        .address_mode   = { state.wrap_u, state.wrap_v, state.wrap_v },
        .lod_bias       = state.lod_bias,
        .max_anisotropy = state.max_anisotropy,
        .debug_label    = "Material sampler"
    };
}

auto sampler_state_from(const erhe::graphics::Sampler_create_info& create_info) -> Material_sampler_state
{
    return Material_sampler_state{
        .wrap_u         = create_info.address_mode[0],
        .wrap_v         = create_info.address_mode[1],
        .min_filter     = create_info.min_filter,
        .mag_filter     = create_info.mag_filter,
        .mipmap_mode    = create_info.mipmap_mode,
        .max_anisotropy = create_info.max_anisotropy,
        .lod_bias       = create_info.lod_bias
    };
}

void Material::set_slot_sampler(Material_texture_sampler& slot, const Material_sampler_state& state)
{
    const Slot_sampler_properties* properties = sampler_properties_of(data.texture_samplers, slot);
    if (properties == nullptr) {
        log_primitive->error("Material '{}': set_slot_sampler with a slot of another material", get_name());
        return;
    }
    const erhe::property::Dependency_object::Change_batch batch{*this};
    apply_sampler_state(*this, *properties, state);
}

namespace {

// The three UV transform properties of one slot, for set_slot_uv_transform.
class Slot_uv_transform_properties
{
public:
    const Property<float>&     rotation;
    const Property<glm::vec2>& offset;
    const Property<glm::vec2>& scale;
};

auto uv_transform_properties_of(const Material_texture_samplers& samplers, const Material_texture_sampler& slot) -> const Slot_uv_transform_properties*
{
    static const Slot_uv_transform_properties c_base_color        {Material::base_color_texture_uv_rotation_property,         Material::base_color_texture_uv_offset_property,         Material::base_color_texture_uv_scale_property};
    static const Slot_uv_transform_properties c_metallic_roughness{Material::metallic_roughness_texture_uv_rotation_property, Material::metallic_roughness_texture_uv_offset_property, Material::metallic_roughness_texture_uv_scale_property};
    static const Slot_uv_transform_properties c_normal            {Material::normal_texture_uv_rotation_property,             Material::normal_texture_uv_offset_property,             Material::normal_texture_uv_scale_property};
    static const Slot_uv_transform_properties c_occlusion         {Material::occlusion_texture_uv_rotation_property,          Material::occlusion_texture_uv_offset_property,          Material::occlusion_texture_uv_scale_property};
    static const Slot_uv_transform_properties c_emissive          {Material::emissive_texture_uv_rotation_property,           Material::emissive_texture_uv_offset_property,           Material::emissive_texture_uv_scale_property};
    if (&slot == &samplers.base_color)         { return &c_base_color; }
    if (&slot == &samplers.metallic_roughness) { return &c_metallic_roughness; }
    if (&slot == &samplers.normal)             { return &c_normal; }
    if (&slot == &samplers.occlusion)          { return &c_occlusion; }
    if (&slot == &samplers.emissive)           { return &c_emissive; }
    return nullptr;
}

} // anonymous namespace

void Material::set_slot_uv_transform(Material_texture_sampler& slot, const float rotation, const glm::vec2& offset, const glm::vec2& scale)
{
    const Slot_uv_transform_properties* properties = uv_transform_properties_of(data.texture_samplers, slot);
    if (properties == nullptr) {
        log_primitive->error("Material '{}': set_slot_uv_transform with a slot of another material", get_name());
        return;
    }
    const erhe::property::Dependency_object::Change_batch batch{*this};
    if (rotation != 0.0f)                { set_value(properties->rotation, rotation); } else { clear_value(properties->rotation); }
    if (offset != glm::vec2{0.0f, 0.0f}) { set_value(properties->offset,   offset);   } else { clear_value(properties->offset);   }
    if (scale  != glm::vec2{1.0f, 1.0f}) { set_value(properties->scale,    scale);    } else { clear_value(properties->scale);    }
}

Material::Material()                           = default;
Material::Material(const Material&)            = default;
Material& Material::operator=(const Material&) = default;
Material::~Material() noexcept                 = default;

Material::Material(const Material_create_info& create_info)
    : Item{create_info.name}
    , data{create_info.data}
{
    set_values(create_info.values);
    seed_slot_values_from_data();
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

// The slot fields of data (a Material_create_info fill) become local
// values; a slot left at its default stays unset, so it can inherit.
void Material::seed_slot_values_from_data()
{
    const erhe::property::Dependency_object::Change_batch batch{*this};
    const auto seed_slot = [this](
        const Material_texture_sampler&   slot,
        const Property<Object_reference>& texture_property,
        const Property<Texgen_mode>&      texgen_mode_property,
        const Property<float>&            rotation_property,
        const Property<glm::vec2>&        offset_property,
        const Property<glm::vec2>&        scale_property
    ) {
        if (slot.texture_reference)                      { set_value(texture_property,     Slot_traits::to_value(slot.texture_reference)); }
        if (slot.texgen_mode != Texgen_mode::uv0)        { set_value(texgen_mode_property, slot.texgen_mode); }
        if (slot.rotation != 0.0f)                       { set_value(rotation_property,    slot.rotation); }
        if (slot.offset != glm::vec2{0.0f, 0.0f})        { set_value(offset_property,      slot.offset); }
        if (slot.scale  != glm::vec2{1.0f, 1.0f})        { set_value(scale_property,       slot.scale); }
        apply_sampler_state(*this, *sampler_properties_of(data.texture_samplers, slot), slot.sampler); // the default fields clear an already unset value
    };
    seed_slot(data.texture_samplers.base_color,         base_color_texture_property,         base_color_texture_texgen_mode_property,         base_color_texture_uv_rotation_property,         base_color_texture_uv_offset_property,         base_color_texture_uv_scale_property);
    seed_slot(data.texture_samplers.metallic_roughness, metallic_roughness_texture_property, metallic_roughness_texture_texgen_mode_property, metallic_roughness_texture_uv_rotation_property, metallic_roughness_texture_uv_offset_property, metallic_roughness_texture_uv_scale_property);
    seed_slot(data.texture_samplers.normal,             normal_texture_property,             normal_texture_texgen_mode_property,             normal_texture_uv_rotation_property,             normal_texture_uv_offset_property,             normal_texture_uv_scale_property);
    seed_slot(data.texture_samplers.occlusion,          occlusion_texture_property,          occlusion_texture_texgen_mode_property,          occlusion_texture_uv_rotation_property,          occlusion_texture_uv_offset_property,          occlusion_texture_uv_scale_property);
    seed_slot(data.texture_samplers.emissive,           emissive_texture_property,           emissive_texture_texgen_mode_property,           emissive_texture_uv_rotation_property,           emissive_texture_uv_offset_property,           emissive_texture_uv_scale_property);
}

// Mirrors a changed slot property's effective value (local, inherited or
// default) into the Material_data slot the per-frame readers use.
void Material::on_property_changed(const erhe::property::Property_changed_args& args)
{
    const erhe::property::Dependency_property* const changed = &args.property;
    const auto mirror_slot = [this, changed](
        Material_texture_sampler&         slot,
        const Property<Object_reference>& texture_property,
        const Property<Texgen_mode>&      texgen_mode_property,
        const Property<float>&            rotation_property,
        const Property<glm::vec2>&        offset_property,
        const Property<glm::vec2>&        scale_property
    ) -> bool {
        if (changed == texture_property.get_ptr())     { slot.texture_reference = Slot_traits::from_value(get_value(texture_property)); return true; }
        if (changed == texgen_mode_property.get_ptr()) { slot.texgen_mode       = get_value(texgen_mode_property); return true; }
        if (changed == rotation_property.get_ptr())    { slot.rotation          = get_value(rotation_property);    return true; }
        if (changed == offset_property.get_ptr())      { slot.offset            = get_value(offset_property);      return true; }
        if (changed == scale_property.get_ptr())       { slot.scale             = get_value(scale_property);       return true; }
        return mirror_sampler_state(*this, *sampler_properties_of(data.texture_samplers, slot), changed, slot.sampler);
    };
    if (mirror_slot(data.texture_samplers.base_color,         base_color_texture_property,         base_color_texture_texgen_mode_property,         base_color_texture_uv_rotation_property,         base_color_texture_uv_offset_property,         base_color_texture_uv_scale_property)) { return; }
    if (mirror_slot(data.texture_samplers.metallic_roughness, metallic_roughness_texture_property, metallic_roughness_texture_texgen_mode_property, metallic_roughness_texture_uv_rotation_property, metallic_roughness_texture_uv_offset_property, metallic_roughness_texture_uv_scale_property)) { return; }
    if (mirror_slot(data.texture_samplers.normal,             normal_texture_property,             normal_texture_texgen_mode_property,             normal_texture_uv_rotation_property,             normal_texture_uv_offset_property,             normal_texture_uv_scale_property)) { return; }
    if (mirror_slot(data.texture_samplers.occlusion,          occlusion_texture_property,          occlusion_texture_texgen_mode_property,          occlusion_texture_uv_rotation_property,          occlusion_texture_uv_offset_property,          occlusion_texture_uv_scale_property)) { return; }
    if (mirror_slot(data.texture_samplers.emissive,           emissive_texture_property,           emissive_texture_texgen_mode_property,           emissive_texture_uv_rotation_property,           emissive_texture_uv_offset_property,           emissive_texture_uv_scale_property)) { return; }
}

Material::Material(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

namespace {

auto to_reference(const std::shared_ptr<erhe::graphics::Texture_reference>& texture) -> Object_reference
{
    return Object_reference{std::dynamic_pointer_cast<erhe::property::Dependency_object>(texture)};
}

} // anonymous namespace

void Material::set_base_color_texture        (const std::shared_ptr<erhe::graphics::Texture_reference>& texture) { set_value(base_color_texture_property,         to_reference(texture)); }
void Material::set_metallic_roughness_texture(const std::shared_ptr<erhe::graphics::Texture_reference>& texture) { set_value(metallic_roughness_texture_property, to_reference(texture)); }
void Material::set_normal_texture            (const std::shared_ptr<erhe::graphics::Texture_reference>& texture) { set_value(normal_texture_property,             to_reference(texture)); }
void Material::set_occlusion_texture         (const std::shared_ptr<erhe::graphics::Texture_reference>& texture) { set_value(occlusion_texture_property,          to_reference(texture)); }
void Material::set_emissive_texture          (const std::shared_ptr<erhe::graphics::Texture_reference>& texture) { set_value(emissive_texture_property,           to_reference(texture)); }

auto Material::get_slot_texture_property(const Material_texture_sampler& slot) const -> const Property<Object_reference>*
{
    const Material_texture_samplers& s = data.texture_samplers;
    if (&slot == &s.base_color)         { return &base_color_texture_property;         }
    if (&slot == &s.metallic_roughness) { return &metallic_roughness_texture_property; }
    if (&slot == &s.normal)             { return &normal_texture_property;             }
    if (&slot == &s.occlusion)          { return &occlusion_texture_property;          }
    if (&slot == &s.emissive)           { return &emissive_texture_property;           }
    return nullptr;
}

void Material::set_slot_texture(Material_texture_sampler& slot, const std::shared_ptr<erhe::graphics::Texture_reference>& texture)
{
    const Property<Object_reference>* property = get_slot_texture_property(slot);
    if (property == nullptr) {
        log_primitive->error("Material '{}': set_slot_texture with a slot of another material", get_name());
        return;
    }
    set_value(*property, to_reference(texture));
}

void Material::set_data(const Material_data& new_data)
{
    const erhe::property::Dependency_object::Change_batch batch{*this};
    const auto apply_slot = [this](
        Material_texture_sampler&         slot,
        const Material_texture_sampler&   new_slot,
        const Property<Object_reference>& texture_property,
        const Property<Texgen_mode>&      texgen_mode_property,
        const Property<float>&            rotation_property,
        const Property<glm::vec2>&        offset_property,
        const Property<glm::vec2>&        scale_property
    ) {
        // A snapshot cannot say "inherit": a slot field at its default
        // (an unbound texture, an identity transform) clears the local value
        // so a folder value shows through; any other value becomes local.
        // The same rule seeds a material from its create info.
        if (new_slot.texture_reference) {
            set_value(texture_property, to_reference(new_slot.texture_reference));
        } else {
            clear_value(texture_property);
        }
        if (new_slot.texgen_mode != Texgen_mode::uv0) { set_value(texgen_mode_property, new_slot.texgen_mode); } else { clear_value(texgen_mode_property); }
        if (new_slot.rotation != 0.0f)                { set_value(rotation_property,    new_slot.rotation);    } else { clear_value(rotation_property);    }
        if (new_slot.offset != glm::vec2{0.0f, 0.0f}) { set_value(offset_property,      new_slot.offset);      } else { clear_value(offset_property);      }
        if (new_slot.scale  != glm::vec2{1.0f, 1.0f}) { set_value(scale_property,       new_slot.scale);       } else { clear_value(scale_property);       }
        apply_sampler_state(*this, *sampler_properties_of(data.texture_samplers, slot), new_slot.sampler);
    };
    apply_slot(data.texture_samplers.base_color,           new_data.texture_samplers.base_color,               base_color_texture_property,            base_color_texture_texgen_mode_property,            base_color_texture_uv_rotation_property,           base_color_texture_uv_offset_property,           base_color_texture_uv_scale_property);
    apply_slot(data.texture_samplers.metallic_roughness,   new_data.texture_samplers.metallic_roughness,       metallic_roughness_texture_property,    metallic_roughness_texture_texgen_mode_property,    metallic_roughness_texture_uv_rotation_property,   metallic_roughness_texture_uv_offset_property,   metallic_roughness_texture_uv_scale_property);
    apply_slot(data.texture_samplers.normal,               new_data.texture_samplers.normal,                   normal_texture_property,                normal_texture_texgen_mode_property,                normal_texture_uv_rotation_property,               normal_texture_uv_offset_property,               normal_texture_uv_scale_property);
    apply_slot(data.texture_samplers.occlusion,            new_data.texture_samplers.occlusion,                occlusion_texture_property,             occlusion_texture_texgen_mode_property,             occlusion_texture_uv_rotation_property,            occlusion_texture_uv_offset_property,            occlusion_texture_uv_scale_property);
    apply_slot(data.texture_samplers.emissive,             new_data.texture_samplers.emissive,                 emissive_texture_property,              emissive_texture_texgen_mode_property,              emissive_texture_uv_rotation_property,             emissive_texture_uv_offset_property,             emissive_texture_uv_scale_property);
}

auto Material::get_values() const -> Material_values
{
    return Material_values{
        .base_color                         = get_base_color(),
        .opacity                            = get_opacity(),
        .roughness                          = get_roughness(),
        .metallic                           = get_metallic(),
        .reflectance                        = get_reflectance(),
        .emissive                           = get_emissive(),
        .ior                                = get_ior(),
        .transmission                       = get_transmission(),
        .normal_texture_scale               = get_normal_texture_scale(),
        .normal_texture_decode_scale        = get_normal_texture_decode_scale(),
        .normal_texture_decode_bias         = get_normal_texture_decode_bias(),
        .normalmap_encoding                 = get_normalmap_encoding(),
        .occlusion_texture_strength         = get_occlusion_texture_strength(),
        .metallic_channel                   = get_metallic_channel(),
        .roughness_channel                  = get_roughness_channel(),
        .occlusion_channel                  = get_occlusion_channel(),
        .opacity_channel                    = get_opacity_channel(),
        .bxdf_model                         = get_bxdf_model(),
        .blending_mode                      = get_blending_mode(),
        .double_sided                       = get_double_sided(),
        .alpha_cutoff                       = get_alpha_cutoff(),
        .use_circular_brushed_metal         = get_use_circular_brushed_metal(),
        .circular_brushed_metal_texgen_mode = get_circular_brushed_metal_texgen_mode(),
        .use_aniso_control                  = get_use_aniso_control()
    };
}

namespace {

// A whole-Material_values snapshot cannot say "inherit", and a value equal
// to the material's own default (D31) is not an authored one
// (doc/property-system.md D32): it clears the local value, so a folder or
// style value shows through and an importer that fills every field from a
// file's defaults leaves the property at Value_source::default_value. The
// same rule set_data applies to the texture slots.
template <typename T>
void set_or_clear(Material& material, const erhe::property::Property<T>& property, const T& value)
{
    if (erhe::property::make_value(value) == material.get_default_value(property.get())) {
        static_cast<void>(material.clear_value(property.get()));
    } else {
        material.set_value(property, value);
    }
}

} // anonymous namespace

void Material::set_values(const Material_values& values)
{
    const erhe::property::Dependency_object::Change_batch batch{*this};
    set_or_clear(*this, base_color_property,                         values.base_color);
    set_or_clear(*this, opacity_property,                            values.opacity);
    set_or_clear(*this, roughness_property,                          values.roughness);
    set_or_clear(*this, metallic_property,                           values.metallic);
    set_or_clear(*this, reflectance_property,                        values.reflectance);
    set_or_clear(*this, emissive_property,                           values.emissive);
    set_or_clear(*this, ior_property,                                values.ior);
    set_or_clear(*this, transmission_property,                       values.transmission);
    set_or_clear(*this, normal_texture_scale_property,               values.normal_texture_scale);
    set_or_clear(*this, normal_texture_decode_scale_property,        values.normal_texture_decode_scale);
    set_or_clear(*this, normal_texture_decode_bias_property,         values.normal_texture_decode_bias);
    set_or_clear(*this, normalmap_encoding_property,                 values.normalmap_encoding);
    set_or_clear(*this, occlusion_texture_strength_property,         values.occlusion_texture_strength);
    set_or_clear(*this, metallic_channel_property,                   values.metallic_channel);
    set_or_clear(*this, roughness_channel_property,                  values.roughness_channel);
    set_or_clear(*this, occlusion_channel_property,                  values.occlusion_channel);
    set_or_clear(*this, opacity_channel_property,                    values.opacity_channel);
    set_or_clear(*this, bxdf_model_property,                         values.bxdf_model);
    set_or_clear(*this, blending_mode_property,                      values.blending_mode);
    set_or_clear(*this, double_sided_property,                       values.double_sided);
    set_or_clear(*this, alpha_cutoff_property,                       values.alpha_cutoff);
    set_or_clear(*this, use_circular_brushed_metal_property,         values.use_circular_brushed_metal);
    set_or_clear(*this, circular_brushed_metal_texgen_mode_property, values.circular_brushed_metal_texgen_mode);
    set_or_clear(*this, use_aniso_control_property,                  values.use_aniso_control);
}

auto Material::to_property_set(const Material_values& values) -> erhe::property::Property_set
{
    using erhe::property::make_value;
    erhe::property::Property_set result;
    result.set(base_color_property,                         make_value(values.base_color));
    result.set(opacity_property,                            make_value(values.opacity));
    result.set(roughness_property,                          make_value(values.roughness));
    result.set(metallic_property,                           make_value(values.metallic));
    result.set(reflectance_property,                        make_value(values.reflectance));
    result.set(emissive_property,                           make_value(values.emissive));
    result.set(ior_property,                                make_value(values.ior));
    result.set(transmission_property,                       make_value(values.transmission));
    result.set(normal_texture_scale_property,               make_value(values.normal_texture_scale));
    result.set(normal_texture_decode_scale_property,        make_value(values.normal_texture_decode_scale));
    result.set(normal_texture_decode_bias_property,         make_value(values.normal_texture_decode_bias));
    result.set(normalmap_encoding_property,                 make_value(values.normalmap_encoding));
    result.set(occlusion_texture_strength_property,         make_value(values.occlusion_texture_strength));
    result.set(metallic_channel_property,                   make_value(values.metallic_channel));
    result.set(roughness_channel_property,                  make_value(values.roughness_channel));
    result.set(occlusion_channel_property,                  make_value(values.occlusion_channel));
    result.set(opacity_channel_property,                    make_value(values.opacity_channel));
    result.set(bxdf_model_property,                         make_value(values.bxdf_model));
    result.set(blending_mode_property,                      make_value(values.blending_mode));
    result.set(double_sided_property,                       make_value(values.double_sided));
    result.set(alpha_cutoff_property,                       make_value(values.alpha_cutoff));
    result.set(use_circular_brushed_metal_property,         make_value(values.use_circular_brushed_metal));
    result.set(circular_brushed_metal_texgen_mode_property, make_value(values.circular_brushed_metal_texgen_mode));
    result.set(use_aniso_control_property,                  make_value(values.use_aniso_control));
    return result;
}

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs)
{
    return
        (lhs.texture_reference == rhs.texture_reference) &&
        (lhs.sampler           == rhs.sampler          ) &&
        (lhs.texgen_mode       == rhs.texgen_mode      ) &&
        (lhs.rotation          == rhs.rotation         ) &&
        (lhs.offset            == rhs.offset           ) &&
        (lhs.scale             == rhs.scale            );

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

[[nodiscard]] auto operator==(const Material_values& lhs, const Material_values& rhs) -> bool
{
    return
        (lhs.base_color                         == rhs.base_color                        ) &&
        (lhs.opacity                            == rhs.opacity                           ) &&
        (lhs.roughness                          == rhs.roughness                         ) &&
        (lhs.metallic                           == rhs.metallic                          ) &&
        (lhs.reflectance                        == rhs.reflectance                       ) &&
        (lhs.emissive                           == rhs.emissive                          ) &&
        (lhs.ior                                == rhs.ior                               ) &&
        (lhs.transmission                       == rhs.transmission                      ) &&
        (lhs.normal_texture_scale               == rhs.normal_texture_scale              ) &&
        (lhs.normal_texture_decode_scale        == rhs.normal_texture_decode_scale       ) &&
        (lhs.normal_texture_decode_bias         == rhs.normal_texture_decode_bias        ) &&
        (lhs.normalmap_encoding                 == rhs.normalmap_encoding                ) &&
        (lhs.occlusion_texture_strength         == rhs.occlusion_texture_strength        ) &&
        (lhs.metallic_channel                   == rhs.metallic_channel                  ) &&
        (lhs.roughness_channel                  == rhs.roughness_channel                 ) &&
        (lhs.occlusion_channel                  == rhs.occlusion_channel                 ) &&
        (lhs.opacity_channel                    == rhs.opacity_channel                   ) &&
        (lhs.bxdf_model                         == rhs.bxdf_model                        ) &&
        (lhs.blending_mode                      == rhs.blending_mode                     ) &&
        (lhs.double_sided                       == rhs.double_sided                      ) &&
        (lhs.alpha_cutoff                       == rhs.alpha_cutoff                      ) &&
        (lhs.use_circular_brushed_metal         == rhs.use_circular_brushed_metal        ) &&
        (lhs.circular_brushed_metal_texgen_mode == rhs.circular_brushed_metal_texgen_mode) &&
        (lhs.use_aniso_control                  == rhs.use_aniso_control                 );
}

[[nodiscard]] auto operator!=(const Material_values& lhs, const Material_values& rhs) -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return lhs.texture_samplers == rhs.texture_samplers;
}

[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool
{
    return
        (lhs.get_name() == rhs.get_name()) &&
        (lhs.data       == rhs.data      ) &&
        (lhs.get_style() == rhs.get_style()) && // D25: the same shared style, or none
        (erhe::property::Property_set::read_local_values(lhs) == erhe::property::Property_set::read_local_values(rhs));
}

[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::primitive
