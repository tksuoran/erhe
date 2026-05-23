#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <memory>
#include <sstream>

namespace erhe::scene_renderer {

auto Shader_key_hash::operator()(const Shader_key& key) const noexcept -> std::size_t
{
    constexpr std::uint64_t seed = 14695981039346656037ull;

    std::uint64_t hash = erhe::hash::hash(&key.bool_mask, sizeof(key.bool_mask), seed);

    hash = erhe::hash::hash(
        key.int_values.data(),
        sizeof(key.int_values),
        hash
    );
    return static_cast<std::size_t>(hash);
}

Shader_key::Shader_key()
{
    std::fill(int_values.begin(), int_values.end(), 0);
}

Shader_key::~Shader_key() noexcept = default;

auto Shader_key::get_defines() const -> std::vector<std::pair<std::string, std::string>>
{
    std::vector<std::pair<std::string, std::string>> defines;

#define ERHE_X(PARAM) \
    if (get(Shader_bool::PARAM)) { \
        defines.emplace_back(std::string{"ERHE_" #PARAM}, std::string{"1"}); \
    }

    ERHE_SHADER_BOOL(ERHE_X)
#undef ERHE_X

#define ERHE_X(PARAM) \
    defines.emplace_back(std::string{"ERHE_" #PARAM}, fmt::format("{}", get(Shader_int::PARAM)));

    ERHE_SHADER_INT(ERHE_X)
#undef ERHE_X

    return defines;
}

[[nodiscard]] auto Shader_key::describe() const -> std::string
{
    const std::vector<std::pair<std::string, std::string>> defines = get_defines();
    std::stringstream ss;
    for (auto & [key, value] : defines) {
        ss << fmt::format("key = {}, value = {}\n", key, value);
    }
    return ss.str();
}

namespace {

[[nodiscard]] auto vertex_format_has_attribute(
    const erhe::dataformat::Vertex_format&         format,
    const erhe::dataformat::Vertex_attribute_usage usage,
    const std::size_t                              usage_index
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

auto Shader_key::derive(
    const erhe::primitive::Material*       material,
    const erhe::dataformat::Vertex_format* vertex_format,
    const bool                             mesh_has_skin
) const -> Shader_key
{
    Shader_key key{};
    key.bool_mask  = bool_mask;
    key.int_values = int_values;

    // Material sub-key (constant for the render call).
    using usage = erhe::dataformat::Vertex_attribute_usage;
    const bool has_normal_0      = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::normal,        erhe::dataformat::normal_attribute);
    const bool has_tangent       = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tangent,       0);
    const bool has_texcoord_0    = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord,     0);
    const bool has_color_0       = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::color,         0);
    const bool has_aniso_control = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::custom,        erhe::dataformat::custom_attribute_aniso_control);
    const bool has_joint_indices = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::joint_indices, 0);
    const bool has_joint_weights = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::joint_weights, 0);

    // Skinning: needs both joint attributes AND the mesh actually has a Skin.
    if (has_joint_indices && has_joint_weights && mesh_has_skin) {
        key.set(Shader_bool::USE_SKINNING, true);
    }

    // Color is always passed through if present
    if (has_color_0) {
        key.set(Shader_bool::USE_VERTEX_VARYING_COLOR, true);
    }

    if (material != nullptr) {
        const erhe::primitive::Material_data&             data     = material->data;
        const erhe::primitive::Material_texture_samplers& samplers = data.texture_samplers;

        // Material booleans -- set per bound sampler.
        if (sampler_is_bound(samplers.base_color))        { key.set(Shader_bool::USE_BASE_COLOR_TEXTURE,         true); }
        if (sampler_is_bound(samplers.metallic_roughness)){ key.set(Shader_bool::USE_METALLIC_ROUGHNESS_TEXTURE, true); }
        if (sampler_is_bound(samplers.normal))            { key.set(Shader_bool::USE_NORMAL_TEXTURE,             true); }
        if (sampler_is_bound(samplers.occlusion))         { key.set(Shader_bool::USE_OCCLUSION_TEXTURE,          true); }
        if (sampler_is_bound(samplers.emissive))          { key.set(Shader_bool::USE_EMISSION_TEXTURE,           true); }

        if (data.use_circular_brushed_metal) {
            key.set(Shader_bool::USE_CIRCULAR_BRUSHED_METAL, true);
        }
        key.set(Shader_int::BXDF_MODEL, static_cast<uint32_t>(data.bxdf_model));

        const bool is_unlit            = data.bxdf_model == erhe::primitive::Bxdf_model::unlit;
        const bool is_anisotropic_brdf =
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_brdf) ||
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_slope) ||
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_engine_ready);

        if (has_normal_0 && !is_unlit) {
            key.set(Shader_bool::USE_VERTEX_VARYING_NORMAL, true);
        }
        const bool needs_tangent_frame =
            sampler_is_bound(samplers.normal) ||
            is_anisotropic_brdf ||
            data.use_circular_brushed_metal;
        if (has_tangent && has_normal_0 && needs_tangent_frame) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TANGENT,   true);
            key.set(Shader_bool::USE_VERTEX_VARYING_BITANGENT, true);
        }
        const bool needs_texcoord_0 =
            any_sampler_uses_tex_coord_zero(*material) ||
            data.use_circular_brushed_metal;
        if (has_texcoord_0 && needs_texcoord_0) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD0, true);
        }

        if (has_aniso_control && data.use_aniso_control) {
            key.set(Shader_bool::USE_VERTEX_VARYING_ANISO_CONTROL, true);
        }
    }

    return key;
}

auto compute_light_layer_partition(std::span<const std::shared_ptr<erhe::scene::Light>> lights) -> Light_layer_partition
{
    auto type_index = [](const erhe::scene::Light_type t) -> std::size_t {
        switch (t) {
            case erhe::scene::Light_type::directional: return 0;
            case erhe::scene::Light_type::spot:        return 1;
            case erhe::scene::Light_type::point:       return 2;
            default:                                   return 3;
        }
    };

    Light_layer_partition partition{};
    for (const std::shared_ptr<erhe::scene::Light>& light : lights) {
        if (!light) {
            continue;
        }
        const std::size_t t = type_index(light->type);
        if (light->cast_shadow) {
            ++partition.per_type_shadow[t];
        } else {
            ++partition.per_type_nonshadow[t];
        }
    }
    return partition;
}

} // namespace erhe::scene_renderer
