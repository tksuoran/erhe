#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <memory>
#include <sstream>

namespace erhe::scene_renderer {

auto Shader_key_hash::operator()(const Shader_key& key) const noexcept -> std::size_t
{
    return static_cast<std::size_t>(key.get_hash());
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

    // VERTEX_POSITION_ENCODING, VERTEX_TEXCOORD_ENCODING and VERTEX_TBN_ENCODING
    // are deliberately excluded: they are emitted by
    // Shader_stages_create_info::attributes_source(), which derives them from the
    // vertex format itself. Emitting them here as well would let a hand-built key
    // that forgot an axis produce a *conflicting* redefinition of the macro.
    // They stay in the key for variant hashing and identity (and describe() still
    // shows them - that re-enumerates ERHE_SHADER_INT independently of this).
#define ERHE_X(PARAM) \
    if constexpr ((Shader_int::PARAM != Shader_int::VERTEX_POSITION_ENCODING) && \
        (Shader_int::PARAM != Shader_int::VERTEX_TEXCOORD_ENCODING) && \
        (Shader_int::PARAM != Shader_int::VERTEX_TBN_ENCODING)      && \
        (Shader_int::PARAM != Shader_int::VERTEX_JOINT_WEIGHTS_ENCODING)) { \
        defines.emplace_back(std::string{"ERHE_" #PARAM}, fmt::format("{}", get(Shader_int::PARAM))); \
    }

    ERHE_SHADER_INT(ERHE_X)
#undef ERHE_X

    return defines;
}

[[nodiscard]] auto Shader_key::get_blending_class() const -> erhe::primitive::Blending_class
{
    // Not value_or(): its argument would construct a Material_values on
    // every call, and this runs per bucket per draw.
    const erhe::primitive::Material_blending_mode mode = blending_mode.has_value()
        ? blending_mode.value()
        : erhe::primitive::Material_values{}.blending_mode;
    return erhe::primitive::get_blending_class(mode);
}

auto Shader_key::describe() const -> std::string
{
    const std::vector<std::pair<std::string, std::string>> defines = get_defines();
    std::stringstream ss;
    if (blending_mode.has_value()) {
        ss << fmt::format("blending_mode = {}\n", c_str(blending_mode.value()));
    }

#define ERHE_X(PARAM) \
    if (get(Shader_bool::PARAM)) { \
        ss << "ERHE_" #PARAM " = 1\n"; \
    } else { \
        ss << "// ERHE_" #PARAM " is not defined\n"; \
    }
    ERHE_SHADER_BOOL(ERHE_X)
#undef ERHE_X

#define ERHE_X(PARAM) ss << fmt::format("ERHE_" #PARAM " = {}\n", get(Shader_int::PARAM));
    ERHE_SHADER_INT(ERHE_X)
#undef ERHE_X

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
    // A bound texture reference (plain Texture or e.g. a Graph_texture)
    // resolves to a texture the material buffer binds, so it selects the
    // texture-using shader variant.
    return static_cast<bool>(s.texture_reference);
}

[[nodiscard]] auto any_sampler_uses_texgen(const erhe::primitive::Material_texture_samplers& s, const erhe::primitive::Texgen_mode texgen_mode) -> bool
{
    auto uses = [texgen_mode](const erhe::primitive::Material_texture_sampler& sampler) {
        return sampler_is_bound(sampler) && (sampler.texgen_mode == texgen_mode);
    };
    return uses(s.base_color)
        || uses(s.metallic_roughness)
        || uses(s.normal)
        || uses(s.occlusion)
        || uses(s.emissive);
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

    // Assign unconditionally rather than only setting the non-zero case: the line
    // above copies int_values from *this, so a stale non-zero encoding on an
    // environment key would otherwise be carried through. derive(material, nullptr,
    // ...) - the material-identity hash - correspondingly pins it to passthrough,
    // which is what that hash wants.
    key.set(
        Shader_int::VERTEX_POSITION_ENCODING,
        static_cast<uint32_t>(erhe::dataformat::get_vertex_position_encoding(vertex_format))
    );
    key.set(
        Shader_int::VERTEX_TEXCOORD_ENCODING,
        static_cast<uint32_t>(erhe::dataformat::get_vertex_texcoord_encoding(vertex_format))
    );

    const erhe::dataformat::Vertex_tbn_encoding tbn_encoding = erhe::dataformat::get_vertex_tbn_encoding(vertex_format);
    key.set(Shader_int::VERTEX_TBN_ENCODING, static_cast<uint32_t>(tbn_encoding));
    key.set(
        Shader_int::VERTEX_JOINT_WEIGHTS_ENCODING,
        static_cast<uint32_t>(erhe::dataformat::get_vertex_joint_weights_encoding(vertex_format))
    );

    using usage = erhe::dataformat::Vertex_attribute_usage;
    // A quaternion-encoded format has NO normal attribute - the frame lives
    // entirely in the tangent slot - so attribute lookup alone would report the
    // optimized variant as normal-less and silently drop every tangent-space
    // and lit shader feature from it. The encoding is what says both are
    // present.
    const bool has_tbn_quaternion = (tbn_encoding == erhe::dataformat::Vertex_tbn_encoding::quaternion16);
    const bool has_normal_0      = has_tbn_quaternion || ((vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::normal,  erhe::dataformat::normal_attribute));
    const bool has_tangent       = has_tbn_quaternion || ((vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tangent, 0));
    const bool has_texcoord_0    = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord,     0);
    const bool has_texcoord_1    = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord,     1);
    const bool has_texcoord_2    = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord,     2);
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

    // Lightmap UVs (channel 2) are not a varying of the primitive key: the
    // scene-level USE_LIGHTMAP environment axis (a lightmap atlas is bound)
    // brings them in on every format that carries them
    // (ERHE_USE_LIGHTMAP_SAMPLING, erhe_standard_variant.glsl), and the
    // per-primitive lightmap_scale_offset decides which draws sample.

    // A primitive without a material draws with the reserved default
    // material record (Material_set::default_material_slot_index), which
    // Material_buffer writes from the Material_values defaults - an opaque
    // isotropic BRDF with no textures. The variant is derived from those same
    // defaults so the shader that reads the record is the lit one the record
    // describes; leaving the material components unset would pick
    // BXDF_MODEL 0 (unlit) with no normal varying, a different material than
    // the record says.
    {
        static const erhe::primitive::Material_texture_samplers s_no_samplers{};
        // Only the fields the key is derived from are read: get_values()
        // resolves every layered material property, and this runs per
        // material per draw-list change and per prewarmed variant.
        erhe::primitive::Material_values data{};
        if (material != nullptr) {
            data.normalmap_encoding                 = material->get_normalmap_encoding();
            data.bxdf_model                         = material->get_bxdf_model();
            data.blending_mode                      = material->get_blending_mode();
            data.use_circular_brushed_metal         = material->get_use_circular_brushed_metal();
            data.circular_brushed_metal_texgen_mode = material->get_circular_brushed_metal_texgen_mode();
            data.use_aniso_control                  = material->get_use_aniso_control();
        }
        const erhe::primitive::Material_texture_samplers& samplers = (material != nullptr) ? material->get_data().texture_samplers : s_no_samplers;

        key.blending_mode = data.blending_mode;

        // Material booleans -- set per bound sampler.
        if (sampler_is_bound(samplers.base_color))        { key.set(Shader_bool::USE_BASE_COLOR_TEXTURE,         true); }
        if (sampler_is_bound(samplers.metallic_roughness)){ key.set(Shader_bool::USE_METALLIC_ROUGHNESS_TEXTURE, true); }
        if (sampler_is_bound(samplers.normal))            {
            key.set(Shader_bool::USE_NORMAL_TEXTURE, true);
            // Start from the material's authored encoding. A KTX2
            // normal-mode texture stores a two component X+Y map whose
            // channel layout is a property of the texture itself (the flag
            // travels on it, so it stays correct however the texture ends
            // up bound): override the channel-count / layout half of the
            // encoding from the texture - X in R / Y in G when the GPU
            // format is two-channel (BC5), X in RGB / Y in A otherwise
            // (RGBA8, ASTC L+A blocks) - while keeping the authored
            // handedness (left handed = Direct3D Y flip).
            erhe::primitive::Normalmap_encoding encoding = data.normalmap_encoding;
            const erhe::graphics::Texture* normal_texture = samplers.normal.texture_reference->get_referenced_texture();
            if ((normal_texture != nullptr) && normal_texture->is_two_component_normal()) {
                const bool xy_in_rg = erhe::dataformat::get_component_count(normal_texture->get_pixelformat()) == 2;
                const bool left_handed =
                    (encoding == erhe::primitive::Normalmap_encoding::left_handed_three_channel) ||
                    (encoding == erhe::primitive::Normalmap_encoding::left_handed_two_channel_ga) ||
                    (encoding == erhe::primitive::Normalmap_encoding::left_handed_two_channel_rg);
                encoding = xy_in_rg
                    ? (left_handed ? erhe::primitive::Normalmap_encoding::left_handed_two_channel_rg
                                   : erhe::primitive::Normalmap_encoding::right_handed_two_channel_rg)
                    : (left_handed ? erhe::primitive::Normalmap_encoding::left_handed_two_channel_ga
                                   : erhe::primitive::Normalmap_encoding::right_handed_two_channel_ga);
            }
            key.set(Shader_int::NORMAL_TEXTURE_ENCODING, to_uint32(encoding));
        }
        if (sampler_is_bound(samplers.occlusion))         { key.set(Shader_bool::USE_OCCLUSION_TEXTURE,          true); }
        if (sampler_is_bound(samplers.emissive))          { key.set(Shader_bool::USE_EMISSION_TEXTURE,           true); }

        // Per-texture texcoord set selection, plumbed as compile-time
        // defines. Set only for bound samplers so unbound textures do not
        // contribute extra shader variants (the axis stays 0).
        if (sampler_is_bound(samplers.base_color))        { key.set(Shader_int::BASE_COLOR_TEXGEN_MODE,         to_uint32(samplers.base_color.texgen_mode));         }
        if (sampler_is_bound(samplers.metallic_roughness)){ key.set(Shader_int::METALLIC_ROUGHNESS_TEXGEN_MODE, to_uint32(samplers.metallic_roughness.texgen_mode)); }
        if (sampler_is_bound(samplers.normal))            { key.set(Shader_int::NORMAL_TEXGEN_MODE,             to_uint32(samplers.normal.texgen_mode));             }
        if (sampler_is_bound(samplers.occlusion))         { key.set(Shader_int::OCCLUSION_TEXGEN_MODE,          to_uint32(samplers.occlusion.texgen_mode));          }
        if (sampler_is_bound(samplers.emissive))          { key.set(Shader_int::EMISSIVE_TEXGEN_MODE,           to_uint32(samplers.emissive.texgen_mode));           }

        if (data.use_circular_brushed_metal) {
            key.set(Shader_bool::USE_CIRCULAR_BRUSHED_METAL, true);
            key.set(Shader_int::CIRCULAR_BRUSHED_METAL_TEXGEN_MODE, to_uint32(data.circular_brushed_metal_texgen_mode));
        }
        key.set(Shader_int::BXDF_MODEL,             static_cast<uint32_t>(data.bxdf_model));
        key.set(Shader_int::MATERIAL_BLENDING_MODE, static_cast<uint32_t>(data.blending_mode));

        const bool is_unlit            = data.bxdf_model == erhe::primitive::Bxdf_model::unlit;
        const bool is_anisotropic_brdf =
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_brdf) ||
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_slope) ||
            (data.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_engine_ready);

        if (has_normal_0 && !is_unlit) {
            key.set(Shader_bool::USE_VERTEX_VARYING_NORMAL, true);
        }

        // A texgen source is needed when any bound sampler reads it or when
        // the circular-brushed-metal block derives its tangent space from
        // it. The alpha_test / screen_door discard path samples base-color
        // alpha, which is covered by the bound-sampler check.
        auto needs_texgen = [&](const erhe::primitive::Texgen_mode texgen_mode) -> bool {
            return
                any_sampler_uses_texgen(samplers, texgen_mode) ||
                (
                    data.use_circular_brushed_metal &&
                    (data.circular_brushed_metal_texgen_mode == texgen_mode)
                );
        };

        // Unlit ignores the normal map / anisotropy entirely, and NORMAL
        // above is gated on !is_unlit: an unlit material with a bound
        // normal texture (e.g. exported LOD assets) must not enable the
        // tangent frame or standard.vert's BITANGENT => TANGENT && NORMAL
        // declaration invariant breaks ('bitangent' undeclared). The tangent
        // texgen mode does not appear here: it projects positions onto its
        // own scale-only frame (v_T_scale_only / v_B_scale_only, gated by
        // ERHE_USE_TANGENT_TEXGEN in standard.vert), which is built from the
        // normal and the node scale and needs neither of these varyings.
        const bool needs_tangent_frame =
            !is_unlit &&
            (
                sampler_is_bound(samplers.normal) ||
                is_anisotropic_brdf ||
                data.use_circular_brushed_metal
            );
        if (has_tangent && has_normal_0 && needs_tangent_frame) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TANGENT,   true);
            key.set(Shader_bool::USE_VERTEX_VARYING_BITANGENT, true);
        }
        if (has_texcoord_0 && needs_texgen(erhe::primitive::Texgen_mode::uv0)) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD0, true);
        }
        if (has_texcoord_1 && needs_texgen(erhe::primitive::Texgen_mode::uv1)) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD1, true);
        }
        if (has_texcoord_2 && needs_texgen(erhe::primitive::Texgen_mode::uv2)) {
            key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD2, true);
        }
        // node_* texgen modes read the untransformed node-space position,
        // which the vertex shader forwards as a dedicated varying (world_*
        // modes reuse v_position, which every lit variant already has).
        if (needs_texgen(erhe::primitive::Texgen_mode::node_xy) ||
            needs_texgen(erhe::primitive::Texgen_mode::node_xz) ||
            needs_texgen(erhe::primitive::Texgen_mode::node_yz))
        {
            key.set(Shader_bool::USE_VERTEX_VARYING_NODE_POSITION, true);
        }

        if (has_aniso_control && data.use_aniso_control) {
            key.set(Shader_bool::USE_VERTEX_VARYING_ANISO_CONTROL, true);
        }
    }

    if (key.get(Shader_int::SHADER_DEBUG) != static_cast<uint32_t>(Shader_debug::none)) {
        apply_shader_debug_varyings(key, vertex_format);
    }

    // The environment's specular anti-aliasing mode reaches only the keys
    // whose shader calls anisotropic_brdf().
    set_specular_anti_aliasing_axis(key, get_specular_anti_aliasing(key));
    // The environment's fp16 paths reach only the lit color variants.
    set_fp16_paths_axis(key, get_fp16_paths(key));

    return key;
}

void apply_shader_debug_varyings(Shader_key& key, const erhe::dataformat::Vertex_format* vertex_format)
{
    // Debug modes visualize raw mesh attributes (normal, tangent,
    // bitangent, texcoords, vertex color, aniso control), so the
    // varyings must reach the fragment shader even when the material
    // would not otherwise request them -- otherwise e.g. the "tangent"
    // mode shows the neutral fallback color on any mesh whose material
    // lacks a normal map / anisotropy. Each set is still gated on the
    // vertex_format carrying the underlying attribute, preserving the
    // ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X invariant. The
    // BITANGENT => TANGENT && NORMAL link-time invariant (bitangent is
    // computed from normal x tangent in standard.vert) is preserved by
    // requiring has_normal_0 for the tangent frame and setting NORMAL
    // alongside it. USE_VERTEX_VARYING_COLOR is set by derive() whenever
    // the format has a color, so vertex-color debug modes are covered.
    using usage = erhe::dataformat::Vertex_attribute_usage;
    const bool has_tbn_quaternion = (erhe::dataformat::get_vertex_tbn_encoding(vertex_format) == erhe::dataformat::Vertex_tbn_encoding::quaternion16);
    const bool has_normal_0       = has_tbn_quaternion || ((vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::normal,  erhe::dataformat::normal_attribute));
    const bool has_tangent        = has_tbn_quaternion || ((vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tangent, 0));
    const bool has_texcoord_0     = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord, 0);
    const bool has_texcoord_1     = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord, 1);
    const bool has_texcoord_2     = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::tex_coord, 2);
    const bool has_aniso_control  = (vertex_format != nullptr) && vertex_format_has_attribute(*vertex_format, usage::custom,    erhe::dataformat::custom_attribute_aniso_control);
    if (has_normal_0) {
        key.set(Shader_bool::USE_VERTEX_VARYING_NORMAL, true);
    }
    if (has_tangent && has_normal_0) {
        key.set(Shader_bool::USE_VERTEX_VARYING_TANGENT,   true);
        key.set(Shader_bool::USE_VERTEX_VARYING_BITANGENT, true);
    }
    if (has_texcoord_0) {
        key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD0, true);
    }
    if (has_texcoord_1) {
        key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD1, true);
    }
    if (has_texcoord_2) {
        key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD2, true);
    }
    if (has_aniso_control) {
        key.set(Shader_bool::USE_VERTEX_VARYING_ANISO_CONTROL, true);
    }
}

void set_fp16_paths_axis(Shader_key& key, const uint32_t mask)
{
    const bool lit_color_variant =
        (key.get(Shader_int::BXDF_MODEL) != static_cast<uint32_t>(erhe::primitive::Bxdf_model::unlit)) &&
        (key.get(Shader_int::SHADER_DEBUG) == static_cast<uint32_t>(Shader_debug::none)) &&
        !key.get(Shader_bool::VARIANT_DEPTH_ONLY) &&
        !key.get(Shader_bool::VARIANT_ID_RENDER) &&
        !key.get(Shader_bool::VARIANT_SHADOW_DISTANCE) &&
        !key.get(Shader_bool::VARIANT_SHADOW_CUBE) &&
        !key.get(Shader_bool::VARIANT_POINTS);
    key.set(Shader_int::FP16_PATHS, lit_color_variant ? (mask & fp16_paths_all) : 0u);
}

auto get_fp16_paths(const Shader_key& key) -> uint32_t
{
    return key.get(Shader_int::FP16_PATHS);
}

void set_specular_anti_aliasing_axis(Shader_key& key, const Specular_anti_aliasing mode)
{
    const uint32_t bxdf_model = key.get(Shader_int::BXDF_MODEL);
    const bool     uses_anisotropic_brdf =
        (
            (bxdf_model == static_cast<uint32_t>(erhe::primitive::Bxdf_model::anisotropic_brdf)) ||
            (bxdf_model == static_cast<uint32_t>(erhe::primitive::Bxdf_model::anisotropic_engine_ready))
        ) &&
        key.get(Shader_bool::USE_VERTEX_VARYING_TANGENT) &&
        key.get(Shader_bool::USE_VERTEX_VARYING_BITANGENT);
    const Specular_anti_aliasing value = uses_anisotropic_brdf ? mode : Specular_anti_aliasing::off;
    key.set(Shader_int::SPECULAR_ANTI_ALIASING, static_cast<uint32_t>(value));
}

auto get_specular_anti_aliasing(const Shader_key& key) -> Specular_anti_aliasing
{
    return static_cast<Specular_anti_aliasing>(key.get(Shader_int::SPECULAR_ANTI_ALIASING));
}

auto c_str(const Light_shading light_shading) -> const char*
{
    switch (light_shading) {
        case Light_shading::loops:     return "loops";
        case Light_shading::clustered: return "clustered";
        case Light_shading::automatic: return "automatic";
        default:                       return "?";
    }
}

auto light_type_index(const erhe::scene::Light_type type) -> std::size_t
{
    switch (type) {
        case erhe::scene::Light_type::directional: return 0;
        case erhe::scene::Light_type::spot:        return 1;
        case erhe::scene::Light_type::point:       return 2;
        default:                                   return 3;
    }
}

auto compute_light_layer_partition(
    std::span<const std::shared_ptr<erhe::scene::Light>> lights,
    const Light_count_limits&                            light_count_limits,
    const std::size_t                                    light_array_capacity
) -> Light_layer_partition
{
    ERHE_PROFILE_FUNCTION();

    // Same walk as Light_projections::apply() pass 2 (which uses the counts
    // produced here as its caps): input order, shadow slot first while the
    // light casts shadows and there is room, else an unshadowed slot while
    // there is room, else the light is not shaded.
    Light_layer_partition partition{};
    partition.light_shading = light_count_limits.light_shading;
    if (partition.light_shading == Light_shading::automatic) {
        const std::size_t active_light_count = static_cast<std::size_t>(std::count_if(
            lights.begin(),
            lights.end(),
            [](const std::shared_ptr<erhe::scene::Light>& light) -> bool {
                return light && light->is_active();
            }
        ));
        partition.light_shading = (active_light_count > automatic_clustered_light_count)
            ? Light_shading::clustered
            : Light_shading::loops;
    }
    const std::size_t max_shaded_count = std::min(light_count_limits.max_light_count, light_array_capacity);
    // Clustered shades every active light: the unshadowed limits do not apply.
    const bool clustered = (partition.light_shading == Light_shading::clustered);
    auto unshadowed_limit = [&](const std::size_t t) -> std::size_t {
        return clustered ? max_shaded_count : light_count_limits.per_type_unshadowed[t];
    };
    std::size_t shaded_count = 0;
    for (const std::shared_ptr<erhe::scene::Light>& light : lights) {
        if (!light) {
            continue;
        }
        if (!light->is_active()) {
            // Inactive light (e.g. a zero-range point light, which reaches
            // nowhere) contributes nothing, so it is counted in neither the
            // shadow nor the non-shadow bucket and the shader's per-type light
            // loops skip it entirely.
            continue;
        }
        if (shaded_count >= max_shaded_count) {
            break;
        }
        const std::size_t t = light_type_index(light->get_light_type());
        if (light->get_cast_shadow() && (partition.per_type_shadow[t] < light_count_limits.per_type_shadow[t])) {
            ++partition.per_type_shadow[t];
            ++shaded_count;
        } else if (partition.per_type_nonshadow[t] < unshadowed_limit(t)) {
            ++partition.per_type_nonshadow[t];
            ++shaded_count;
        }
    }
    return partition;
}

void set_light_partition_axes(Shader_key& key, const Light_layer_partition& partition)
{
    ERHE_VERIFY(partition.light_shading != Light_shading::automatic);
    const bool clustered = (partition.light_shading == Light_shading::clustered);
    auto count = [clustered](const std::size_t value) -> uint32_t {
        return clustered ? 0u : static_cast<uint32_t>(value);
    };
    key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED, count(partition.per_type_nonshadow[0]));
    key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED,     count(partition.per_type_shadow   [0]));
    key.set(Shader_int::LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED,        count(partition.per_type_nonshadow[1]));
    key.set(Shader_int::LIGHT_COUNT_SPOT_SHADOWMAPPED,            count(partition.per_type_shadow   [1]));
    key.set(Shader_int::LIGHT_COUNT_POINT_NOT_SHADOWMAPPED,       count(partition.per_type_nonshadow[2]));
    key.set(Shader_int::LIGHT_COUNT_POINT_SHADOWMAPPED,           count(partition.per_type_shadow   [2]));
    key.set(Shader_bool::LIGHT_SHADING_CLUSTERED,                 clustered);
}

auto get_shadow_depth_bits_axis(const erhe::dataformat::Format depth_format) -> uint32_t
{
    switch (depth_format) {
        case erhe::dataformat::Format::format_d32_sfloat:
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint: {
            return 32u;
        }
        case erhe::dataformat::Format::format_d16_unorm:
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32:
        case erhe::dataformat::Format::format_d24_unorm_s8_uint: {
            const uint32_t depth_bits = static_cast<uint32_t>(erhe::dataformat::get_depth_size_bits(depth_format));
            ERHE_VERIFY((depth_bits > 0u) && (depth_bits < 32u));
            return depth_bits;
        }
        default: {
            return 0u;
        }
    }
}

} // namespace erhe::scene_renderer
