#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace erhe::scene_renderer {

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
    /* Mesh booleans -- function of the mesh's Vertex_format AND the material's needs */    \
    X(USE_SKINNING,                   use_skinning)                                         \
    X(USE_VERTEX_VARYING_NORMAL,      use_vertex_varying_normal)                            \
    X(USE_VERTEX_VARYING_TANGENT,     use_vertex_varying_tangent)                           \
    X(USE_VERTEX_VARYING_BITANGENT,   use_vertex_varying_bitangent)                         \
    X(USE_VERTEX_VARYING_TEXCOORD0,   use_vertex_varying_texcoord0)                         \
    X(USE_VERTEX_VARYING_COLOR,       use_vertex_varying_color)

#define ERHE_STANDARD_VARIANT_COUNT_AXES(X)                                                 \
    /* Scene integer counts -- exact (no tier rounding) */                                  \
    X(LIGHT_DIRECTIONAL_COUNT,              directional_light_count)                        \
    X(LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT, directional_shadowmapped_count)                 \
    X(LIGHT_SPOT_COUNT,                     spot_light_count)                               \
    X(LIGHT_SPOT_SHADOWMAPPED_COUNT,        spot_shadowmapped_count)                        \
    X(LIGHT_POINT_COUNT,                    point_light_count)                              \
    X(LIGHT_POINT_SHADOWMAPPED_COUNT,       point_shadowmapped_count)

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
// loop bounds.
[[nodiscard]] auto make_standard_variant_defines(const Standard_variant_key& key)
    -> std::vector<std::pair<std::string, std::string>>;

} // namespace erhe::scene_renderer
