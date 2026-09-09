#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

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
    if ((Shader_int::PARAM != Shader_int::VERTEX_POSITION_ENCODING) && \
        (Shader_int::PARAM != Shader_int::VERTEX_TEXCOORD_ENCODING) && \
        (Shader_int::PARAM != Shader_int::VERTEX_TBN_ENCODING)      && \
        (Shader_int::PARAM != Shader_int::VERTEX_JOINT_WEIGHTS_ENCODING)) { \
        defines.emplace_back(std::string{"ERHE_" #PARAM}, fmt::format("{}", get(Shader_int::PARAM))); \
    }

    ERHE_SHADER_INT(ERHE_X)
#undef ERHE_X

    return defines;
}

[[nodiscard]] auto Shader_key::describe() const -> std::string
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

    // Lightmap UVs (channel 2) always pass through when the format carries
    // them: whether a draw actually samples the lightmap is a per-primitive
    // runtime gate (primitive.lightmap_scale_offset), not a variant axis,
    // so the varying must exist on every lightmap-capable mesh.
    if (has_texcoord_2) {
        key.set(Shader_bool::USE_VERTEX_VARYING_TEXCOORD2, true);
    }

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
        const erhe::primitive::Material_values            data     = (material != nullptr) ? material->get_values()           : erhe::primitive::Material_values{};
        const erhe::primitive::Material_texture_samplers& samplers = (material != nullptr) ? material->data.texture_samplers : s_no_samplers;

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

    // When any debug visualization is active, expose every varying the
    // mesh can supply, regardless of what the bound material consumes.
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
    // alongside it.
    const bool shader_debug_active = key.get(Shader_int::SHADER_DEBUG) != static_cast<uint32_t>(Shader_debug::none);
    if (shader_debug_active) {
        if (has_normal_0) {
            key.set(Shader_bool::USE_VERTEX_VARYING_NORMAL, true);
        }
        if (has_tangent && has_normal_0) {
            key.set(Shader_bool::USE_VERTEX_VARYING_NORMAL,    true);
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
        // USE_VERTEX_VARYING_COLOR is already set unconditionally above
        // when has_color_0, so vertex-color debug modes are covered.
    }

    return key;
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
    const Light_count_limits&                            light_count_limits
) -> Light_layer_partition
{
    ERHE_PROFILE_FUNCTION();

    // Same walk as Light_projections::apply() pass 2 (which uses the counts
    // produced here as its caps): input order, shadow slot first while the
    // light casts shadows and there is room, else an unshadowed slot while
    // there is room, else the light is not shaded.
    Light_layer_partition partition{};
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
        const std::size_t t = light_type_index(light->get_light_type());
        if (light->get_cast_shadow() && (partition.per_type_shadow[t] < light_count_limits.per_type_shadow[t])) {
            ++partition.per_type_shadow[t];
        } else if (partition.per_type_nonshadow[t] < light_count_limits.per_type_unshadowed[t]) {
            ++partition.per_type_nonshadow[t];
        }
    }
    return partition;
}

} // namespace erhe::scene_renderer
