#include "erhe_scene_renderer/standard_shader_variant.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"

#include <memory>

namespace erhe::scene_renderer {

namespace {

constexpr std::size_t s_count_axis_count =
#define ERHE_X(NAME, FIELD) +1
    0 ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X
    ;

[[nodiscard]] auto fnv1a64(const void* data, std::size_t size, std::uint64_t seed) noexcept -> std::uint64_t
{
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = seed;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

} // anonymous namespace

auto Standard_variant_key_hash::operator()(const Standard_variant_key& key) const noexcept -> std::size_t
{
    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;

    std::uint64_t hash = fnv1a64(&key.boolean_mask, sizeof(key.boolean_mask), fnv_offset_basis);

    // Pack the count axes into a small contiguous array so the hash is
    // stable regardless of struct padding. uint16_t fields are read from
    // the key directly via the same X-macro list.
    std::uint16_t count_buffer[s_count_axis_count];
    std::size_t   index = 0;
#define ERHE_X(NAME, FIELD) count_buffer[index++] = key.FIELD;
    ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X

    hash = fnv1a64(count_buffer, sizeof(count_buffer), hash);
    return static_cast<std::size_t>(hash);
}

auto make_standard_variant_defines(const Standard_variant_key& key)
    -> std::vector<std::pair<std::string, std::string>>
{
    std::vector<std::pair<std::string, std::string>> defines;

    // Build-mode marker so erhe_standard_variant.glsl skips its
    // non-variant fallbacks and uses what we emit here instead.
    defines.emplace_back(std::string{"ERHE_STANDARD_VARIANT_BUILD"}, std::string{"1"});

    // Boolean axes -- emit `#define ERHE_<NAME> 1` for each set bit so
    // the shader's `#ifdef ERHE_<NAME>` / `#if ERHE_<NAME>` checks both
    // resolve. Disabled bits are not #defined (otherwise `#ifdef` would
    // see them as enabled), but a comment line is appended to the
    // preamble so RenderDoc captures show the full key in the
    // generated source -- the empty-name convention is recognized by
    // Shader_stages_create_info::final_source as "emit value as a //
    // comment, no #define".
#define ERHE_X(NAME, FIELD)                                                                       \
    if (key.get_boolean(Standard_variant_key::Boolean_axis::FIELD)) {                             \
        defines.emplace_back(std::string{"ERHE_" #NAME}, std::string{"1"});                       \
    } else {                                                                                      \
        defines.emplace_back(std::string{}, std::string{"ERHE_" #NAME " disabled"});              \
    }
    ERHE_STANDARD_VARIANT_BOOL_AXES(ERHE_X)
#undef ERHE_X

    // Count axes -- always emit so the shader can rely on them as
    // compile-time loop bounds.
#define ERHE_X(NAME, FIELD)                                                            \
    defines.emplace_back(std::string{"ERHE_" #NAME}, std::to_string(key.FIELD));
    ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X

    return defines;
}

namespace {

[[nodiscard]] auto vertex_format_has_attribute(
    const erhe::dataformat::Vertex_format&       format,
    const erhe::dataformat::Vertex_attribute_usage usage,
    const std::size_t                            usage_index
) -> bool
{
    return format.find_attribute(usage, usage_index).attribute != nullptr;
}

[[nodiscard]] auto sampler_is_bound(const erhe::primitive::Material_texture_sampler& s) -> bool
{
    return static_cast<bool>(s.texture);
}

[[nodiscard]] auto any_sampler_uses_tex_coord_zero(const erhe::primitive::Material& material) -> bool
{
    const erhe::primitive::Material_texture_samplers& s = material.data.texture_samplers;
    auto uses_zero = [](const erhe::primitive::Material_texture_sampler& sampler) {
        return sampler_is_bound(sampler) && (sampler.tex_coord == 0u);
    };
    return uses_zero(s.base_color)
        || uses_zero(s.metallic_roughness)
        || uses_zero(s.normal)
        || uses_zero(s.occlusion)
        || uses_zero(s.emissive);
}

} // anonymous namespace

auto compute_standard_variant_key(
    const erhe::primitive::Material*       material,
    const erhe::dataformat::Vertex_format& vertex_format,
    const bool                             mesh_has_skin,
    const Standard_variant_light_counts&   light_counts
) -> Standard_variant_key
{
    using Axis = Standard_variant_key::Boolean_axis;

    Standard_variant_key key{};

    // Scene sub-key (constant for the render call).
    key.directional_light_count        = light_counts.directional_light_count;
    key.directional_shadowmapped_count = light_counts.directional_shadowmapped_count;
    key.spot_light_count               = light_counts.spot_light_count;
    key.spot_shadowmapped_count        = light_counts.spot_shadowmapped_count;
    key.point_light_count              = light_counts.point_light_count;
    key.point_shadowmapped_count       = light_counts.point_shadowmapped_count;

    using usage = erhe::dataformat::Vertex_attribute_usage;
    const bool has_position = vertex_format_has_attribute(vertex_format, usage::position, 0);
    const bool has_normal_0 = vertex_format_has_attribute(vertex_format, usage::normal,   erhe::dataformat::normal_attribute);
    const bool has_tangent  = vertex_format_has_attribute(vertex_format, usage::tangent,  0);
    const bool has_texcoord_0 = vertex_format_has_attribute(vertex_format, usage::tex_coord, 0);
    const bool has_color_0  = vertex_format_has_attribute(vertex_format, usage::color,    0);
    const bool has_aniso_control = vertex_format_has_attribute(vertex_format, usage::custom, erhe::dataformat::custom_attribute_aniso_control);
    const bool has_joint_indices = vertex_format_has_attribute(vertex_format, usage::joint_indices, 0);
    const bool has_joint_weights = vertex_format_has_attribute(vertex_format, usage::joint_weights, 0);
    static_cast<void>(has_position); // position is required by the shader; not a variant axis

    // Skinning: needs both joint attributes AND the mesh actually has a Skin.
    if (has_joint_indices && has_joint_weights && mesh_has_skin) {
        key.set_boolean(Axis::use_skinning, true);
    }

    // Color is always passed through if present (gating deferred per plan).
    if (has_color_0) {
        key.set_boolean(Axis::use_vertex_varying_color, true);
    }

    if (material == nullptr) {
        // No material bound: emit a minimal key (counts already set).
        // The vertex normal varying is still useful for unlit visuals
        // when present, but the lit/unlit branch in the shader treats
        // missing material as "fully unlit", so leaving it off is fine.
        return key;
    }

    const erhe::primitive::Material_data&             data     = material->data;
    const erhe::primitive::Material_texture_samplers& samplers = data.texture_samplers;

    // Material booleans -- set per bound sampler.
    if (sampler_is_bound(samplers.base_color))         { key.set_boolean(Axis::use_base_color_texture,         true); }
    if (sampler_is_bound(samplers.metallic_roughness)) { key.set_boolean(Axis::use_metallic_roughness_texture, true); }
    if (sampler_is_bound(samplers.normal))             { key.set_boolean(Axis::use_normal_texture,             true); }
    if (sampler_is_bound(samplers.occlusion))          { key.set_boolean(Axis::use_occlusion_texture,          true); }
    if (sampler_is_bound(samplers.emissive))           { key.set_boolean(Axis::use_emission_texture,           true); }

    // Material shading-model axes.
    if (data.use_circular_brushed_metal) {
        key.set_boolean(Axis::use_circular_brushed_metal, true);
    }
    key.bxdf_model = static_cast<uint16_t>(data.bxdf_model);

    const bool is_unlit            = data.bxdf_model == erhe::primitive::Bxdf_model::unlit;
    const bool is_anisotropic_brdf = data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_brdf;

    // Mesh sub-key axes that depend on the bound material.
    if (has_normal_0 && !is_unlit) {
        key.set_boolean(Axis::use_vertex_varying_normal, true);
    }
    // Tangent + bitangent come together: the bitangent varying carries the
    // sign-corrected cross product computed in the vertex stage. They're
    // useful when a normal map sampler is bound, when the BxDF is
    // anisotropic (anisotropic_brdf needs T and B as inputs), or when a
    // tangent-frame-rewriting effect (e.g. circular brushed metal) is on.
    const bool needs_tangent_frame =
        sampler_is_bound(samplers.normal) ||
        is_anisotropic_brdf ||
        data.use_circular_brushed_metal;
    if (has_tangent && has_normal_0 && needs_tangent_frame) {
        key.set_boolean(Axis::use_vertex_varying_tangent,   true);
        key.set_boolean(Axis::use_vertex_varying_bitangent, true);
    }
    // Texcoord 0 is needed when any bound sampler uses it OR when circular
    // brushed metal is on (the brush direction is derived from UV).
    const bool needs_texcoord_0 =
        any_sampler_uses_tex_coord_zero(*material) ||
        data.use_circular_brushed_metal;
    if (has_texcoord_0 && needs_texcoord_0) {
        key.set_boolean(Axis::use_vertex_varying_texcoord0, true);
    }

    // Aniso control varying: enabled when the mesh ships per-vertex aniso
    // data (custom_attribute_aniso_control) AND the material consumes it.
    if (has_aniso_control && data.use_aniso_control) {
        key.set_boolean(Axis::use_vertex_varying_aniso_control, true);
    }

    return key;
}

auto compute_standard_variant_light_counts(const erhe::scene::Light_layer& layer)
    -> Standard_variant_light_counts
{
    auto type_index = [](const erhe::scene::Light_type t) -> std::size_t {
        switch (t) {
            case erhe::scene::Light_type::directional: return 0;
            case erhe::scene::Light_type::spot:        return 1;
            case erhe::scene::Light_type::point:       return 2;
            default:                                   return 3;
        }
    };

    std::size_t per_type_shadow   [4] = {0, 0, 0, 0};
    std::size_t per_type_nonshadow[4] = {0, 0, 0, 0};
    for (const std::shared_ptr<erhe::scene::Light>& light : layer.lights) {
        if (!light) {
            continue;
        }
        const std::size_t t = type_index(light->type);
        if (light->cast_shadow) {
            ++per_type_shadow[t];
        } else {
            ++per_type_nonshadow[t];
        }
    }

    Standard_variant_light_counts result{};
    result.directional_shadowmapped_count = static_cast<uint16_t>(per_type_shadow[0]);
    result.directional_light_count        = static_cast<uint16_t>(per_type_shadow[0] + per_type_nonshadow[0]);
    result.spot_shadowmapped_count        = static_cast<uint16_t>(per_type_shadow[1]);
    result.spot_light_count               = static_cast<uint16_t>(per_type_shadow[1] + per_type_nonshadow[1]);
    result.point_shadowmapped_count       = static_cast<uint16_t>(per_type_shadow[2]);
    result.point_light_count              = static_cast<uint16_t>(per_type_shadow[2] + per_type_nonshadow[2]);
    return result;
}

} // namespace erhe::scene_renderer
