#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::dataformat {
    class Vertex_format;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Light;
    class Light_layer;
}

namespace erhe::scene_renderer {

// Per-viewport debug-visualization selection that replaces the
// standalone standard_debug.frag's ERHE_DEBUG_* defines. Each enum
// value compiles a distinct variant of the standard shader that
// overrides the final fragment color. Keep in sync with the
// ERHE_SHADER_DEBUG_* macros in erhe_standard_variant.glsl and with
// the c_shader_debug_strings table consumers (editor UI).
enum class Shader_debug : uint16_t
{
    none                 = 0,
    vertex_normal        = 1,
    fragment_normal      = 2,
    normal_texture       = 3,
    tangent              = 4,
    vertex_tangent_w     = 5,
    bitangent            = 6,
    texcoord             = 7,
    base_color_texture   = 8,
    vertex_color_rgb     = 9,
    vertex_color_alpha   = 10,
    aniso_strength       = 11,
    aniso_texcoord       = 12,
    vdotn                = 13,
    ldotn                = 14,
    hdotv                = 15,
    joint_indices        = 16,
    joint_weights        = 17,
    omega_o              = 18,
    omega_i              = 19,
    omega_g              = 20,
    vertex_valency       = 21,
    polygon_edge_count   = 22,
    metallic             = 23,
    roughness            = 24,
    occlusion            = 25,
    emissive             = 26,
    shadowmap_texels     = 27,
    misc                 = 28
};

// User-visible display strings matching the Shader_debug enum, in
// enum order. Used by the Scene_view_config_window combo.
inline constexpr const char* c_shader_debug_strings[] = {
    "None",
    "Vertex Normal",
    "Fragment Normal",
    "Normal Texture",
    "Tangent",
    "Vertex Tangent W",
    "Bitangent",
    "TexCoord",
    "Base Color Texture",
    "Vertex Color RGB",
    "Vertex Color Alpha",
    "Aniso Strength",
    "Aniso TexCoord",
    "V.N",
    "L.N",
    "H.V",
    "Joint Indices",
    "Joint Weights",
    "Omega o",
    "Omega i",
    "Omega g",
    "Vertex Valency",
    "Polygon Edge Count",
    "Metallic",
    "Roughness",
    "Occlusion",
    "Emissive",
    "Shadowmap Texels",
    "Debug Miscellaneous"
};

// X-macro list of variant axes for the editor's standard lit shader.
//
// Single source of truth: the key struct fields, the Boolean_axis enum, the
// GLSL define emitter, and any future debug UI all derive from these lists.
// Adding an axis here is a coordinated change with the shader source -- the
// shader must read the new define / use it as a compile-time loop bound, so
// keeping the axes in one place is the right granularity.
//
// The NAME column is the GLSL macro suffix; the emitter prepends "ERHE_".
// The FIELD column is the key struct field / Boolean_axis enumerator.
#define ERHE_STANDARD_VARIANT_BOOL_AXES(X)                                                  \
    /* Material booleans -- which texture samplers the bound material uses */               \
    X(USE_BASE_COLOR_TEXTURE,         use_base_color_texture)                               \
    X(USE_METALLIC_ROUGHNESS_TEXTURE, use_metallic_roughness_texture)                       \
    X(USE_NORMAL_TEXTURE,             use_normal_texture)                                   \
    X(USE_OCCLUSION_TEXTURE,          use_occlusion_texture)                                \
    X(USE_EMISSION_TEXTURE,           use_emission_texture)                                 \
    /* Material shading variants -- compile-time selection of shading model */              \
    X(USE_CIRCULAR_BRUSHED_METAL,     use_circular_brushed_metal)                           \
    /* Mesh booleans -- function of the mesh's Vertex_format AND the material's needs */    \
    X(USE_SKINNING,                   use_skinning)                                         \
    X(USE_VERTEX_VARYING_NORMAL,      use_vertex_varying_normal)                            \
    X(USE_VERTEX_VARYING_TANGENT,     use_vertex_varying_tangent)                           \
    X(USE_VERTEX_VARYING_BITANGENT,   use_vertex_varying_bitangent)                         \
    X(USE_VERTEX_VARYING_TEXCOORD0,   use_vertex_varying_texcoord0)                         \
    X(USE_VERTEX_VARYING_COLOR,       use_vertex_varying_color)                             \
    X(USE_VERTEX_VARYING_ANISO_CONTROL, use_vertex_varying_aniso_control)

#define ERHE_STANDARD_VARIANT_COUNT_AXES(X)                                                 \
    /* Scene integer counts -- exact (no tier rounding) */                                  \
    X(LIGHT_DIRECTIONAL_COUNT,              directional_light_count)                        \
    X(LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT, directional_shadowmapped_count)                 \
    X(LIGHT_SPOT_COUNT,                     spot_light_count)                               \
    X(LIGHT_SPOT_SHADOWMAPPED_COUNT,        spot_shadowmapped_count)                        \
    X(LIGHT_POINT_COUNT,                    point_light_count)                              \
    X(LIGHT_POINT_SHADOWMAPPED_COUNT,       point_shadowmapped_count)                       \
    /* Material BxDF selection -- discrete enum encoded as a count axis    */               \
    /* (0 = unlit, 1 = isotropic_brdf, 2 = anisotropic_brdf, 3 = slope,    */               \
    /*  4 = engine_ready). Mirrors erhe::primitive::Bxdf_model and the    */                \
    /* ERHE_BXDF_MODEL_* GLSL macros in erhe_standard_variant.glsl.       */                \
    X(BXDF_MODEL,                           bxdf_model)                                     \
    /* Per-viewport debug visualization. 0 = none, 1..N = ERHE_DEBUG_*    */                \
    /* overrides absorbed from the retired standard_debug.frag. Mirrors  */                 \
    /* Shader_debug above and the ERHE_SHADER_DEBUG_* GLSL macros.       */                 \
    X(SHADER_DEBUG,                         shader_debug)

class Standard_variant_key
{
public:
    enum class Boolean_axis : uint32_t {
#define ERHE_X(NAME, FIELD) FIELD,
        ERHE_STANDARD_VARIANT_BOOL_AXES(ERHE_X)
#undef ERHE_X
        boolean_axis_count
    };

    static_assert(
        static_cast<uint32_t>(Boolean_axis::boolean_axis_count) <= 32,
        "Standard_variant_key: boolean axes must fit in a uint32_t bit-mask"
    );

    [[nodiscard]] auto get_boolean(Boolean_axis axis) const -> bool
    {
        return (boolean_mask & (uint32_t{1} << static_cast<uint32_t>(axis))) != 0;
    }
    void set_boolean(Boolean_axis axis, bool value)
    {
        const uint32_t bit = uint32_t{1} << static_cast<uint32_t>(axis);
        if (value) {
            boolean_mask |= bit;
        } else {
            boolean_mask &= ~bit;
        }
    }

    uint32_t boolean_mask{0};

#define ERHE_X(NAME, FIELD) uint16_t FIELD{0};
    ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X

    [[nodiscard]] auto operator==(const Standard_variant_key& other) const noexcept -> bool = default;
};

class Standard_variant_key_hash
{
public:
    [[nodiscard]] auto operator()(const Standard_variant_key& key) const noexcept -> std::size_t;
};

// Emit the GLSL #define list (NAME, value) pairs for a key. Booleans are
// emitted only when set ("ERHE_<NAME>" = "1"); counts are emitted always
// ("ERHE_<NAME>" = "<integer>") so the shader can use them as compile-time
// loop bounds. Always emits ERHE_STANDARD_VARIANT_BUILD = 1 so the shader
// source can suppress its non-variant fallback definitions.
[[nodiscard]] auto make_standard_variant_defines(const Standard_variant_key& key)
    -> std::vector<std::pair<std::string, std::string>>;

// Per-frame light count snapshot used to populate the scene sub-key.
// Mirrors the six light_block runtime fields the standard shader reads.
class Standard_variant_light_counts
{
public:
    uint16_t directional_light_count        {0};
    uint16_t directional_shadowmapped_count {0};
    uint16_t spot_light_count               {0};
    uint16_t spot_shadowmapped_count        {0};
    uint16_t point_light_count              {0};
    uint16_t point_shadowmapped_count       {0};
};

// Walk a Light_layer's lights, partition them by (Light::type,
// cast_shadow), and produce the count snapshot the standard shader
// variant cache keys on. Used by the init-time prewarm paths
// (renderers/prewarm.cpp, preview/scene_preview.cpp).
[[nodiscard]] auto compute_standard_variant_light_counts(
    const erhe::scene::Light_layer& layer
) -> Standard_variant_light_counts;

// Per-type partition of a light span. Indices 0..3 follow the
// canonical type_index_of mapping used by both light_buffer.cpp and
// the variant-count helper (0=directional, 1=spot, 2=point, 3=other).
// Both arrays sum to the number of non-null lights in the input span.
class Light_layer_partition
{
public:
    std::size_t per_type_shadow   [4] {0, 0, 0, 0};
    std::size_t per_type_nonshadow[4] {0, 0, 0, 0};
};

// Shared per-type / per-shadow tally used by Light_projections::apply
// (for UBO slot assignment) and by compute_standard_variant_light_counts
// (for the variant-cache key). Lifts the previously-duplicated tally
// into one helper so the two consumers cannot drift.
[[nodiscard]] auto compute_light_layer_partition(
    std::span<const std::shared_ptr<erhe::scene::Light>> lights
) -> Light_layer_partition;

// Build a Standard_variant_key from a (material, vertex_format, has_skin,
// scene-light-counts) tuple.
//
//  - Material booleans (USE_*_TEXTURE) are set when the corresponding
//    Material_texture_sampler::texture is bound.
//  - USE_SKINNING is set when the vertex_format declares both
//    joint_indices_0 and joint_weights_0 AND mesh_has_skin is true.
//  - USE_VERTEX_VARYING_NORMAL is set when the vertex_format has a normal
//    attribute (any usage_index) AND the material is not unlit.
//  - USE_VERTEX_VARYING_TANGENT / _BITANGENT are set when the vertex_format
//    has both normal and tangent AND the material consumes a normal map
//    sampler. (Future aniso material work will extend this condition.)
//  - USE_VERTEX_VARYING_TEXCOORD0 is set when the vertex_format has
//    tex_coord index 0 AND any bound material sampler has tex_coord==0.
//  - USE_VERTEX_VARYING_COLOR is set when the vertex_format has color
//    index 0 (gating left for later per the plan).
//  - USE_VERTEX_VARYING_ANISO_CONTROL is set when the vertex_format has
//    the custom_attribute_aniso_control attribute AND
//    material.use_aniso_control is true.
//
// The function honors the plan invariant ERHE_USE_VERTEX_VARYING_X =>
// ERHE_ATTRIBUTE_a_X (the underlying attribute is always required for
// the corresponding USE flag).
[[nodiscard]] auto compute_standard_variant_key(
    const erhe::primitive::Material*       material,
    const erhe::dataformat::Vertex_format& vertex_format,
    bool                                   mesh_has_skin,
    const Standard_variant_light_counts&   light_counts
) -> Standard_variant_key;

} // namespace erhe::scene_renderer
