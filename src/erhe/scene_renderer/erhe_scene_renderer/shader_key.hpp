#pragma once

#include "erhe_hash/hash.hpp"
#include "erhe_primitive/enums.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::dataformat { class Vertex_format; }
namespace erhe::primitive  { class Material; }
namespace erhe::scene {
    class Light;
    class Light_layer;
}

namespace erhe::scene_renderer {

// Per-viewport debug-visualization selection that replaces the
// shader ERHE_DEBUG_* defines. Each enum value compiles a distinct
// variant of the shader that overrides the final fragment color.
// Keep in sync with the ERHE_SHADER_DEBUG_* macros in shader and with
// the c_shader_debug_strings table consumers (editor UI).
enum class Shader_debug : uint16_t
{
    none               = 0,
    vertex_normal      = 1,
    fragment_normal    = 2,
    normal_texture     = 3,
    tangent            = 4,
    vertex_tangent_w   = 5,
    bitangent          = 6,
    texcoord           = 7,
    base_color_texture = 8,
    vertex_color_rgb   = 9,
    vertex_color_alpha = 10,
    aniso_strength     = 11,
    aniso_texcoord     = 12,
    vdotn              = 13,
    ldotn              = 14,
    hdotv              = 15,
    joint_indices      = 16,
    joint_weights      = 17,
    omega_o            = 18,
    omega_i            = 19,
    omega_g            = 20,
    vertex_valency     = 21,
    polygon_edge_count = 22,
    metallic           = 23,
    roughness          = 24,
    occlusion          = 25,
    emissive           = 26,
    shadowmap_texels   = 27,
    misc               = 28
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

#define ERHE_SHADER_BOOL(X) \
    X(USE_BASE_COLOR_TEXTURE)           \
    X(USE_METALLIC_ROUGHNESS_TEXTURE)   \
    X(USE_NORMAL_TEXTURE)               \
    X(USE_OCCLUSION_TEXTURE)            \
    X(USE_EMISSION_TEXTURE)             \
    X(USE_CIRCULAR_BRUSHED_METAL)       \
    X(USE_SKINNING)                     \
    X(USE_VERTEX_VARYING_NORMAL)        \
    X(USE_VERTEX_VARYING_TANGENT)       \
    X(USE_VERTEX_VARYING_BITANGENT)     \
    X(USE_VERTEX_VARYING_TEXCOORD0)     \
    X(USE_VERTEX_VARYING_COLOR)         \
    X(USE_VERTEX_VARYING_ANISO_CONTROL) \
    X(VARIANT_DEPTH_ONLY)               \
    X(VARIANT_ID_RENDER)                \
    X(VARIANT_BRUSH_PREVIEW)            \
    X(VARIANT_RENDERTARGET)

#define ERHE_SHADER_INT(X) \
    X(LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED)     \
    X(LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED) \
    X(LIGHT_COUNT_SPOT_SHADOWMAPPED)            \
    X(LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED)        \
    X(LIGHT_COUNT_POINT_SHADOWMAPPED)           \
    X(LIGHT_COUNT_POINT_NOT_SHADOWMAPPED)       \
    X(BXDF_MODEL)                               \
    X(MATERIAL_BLENDING_MODE)                   \
    X(SHADER_DEBUG)                             \
    X(SHADER_MULTIVIEW_COUNT)

enum class Shader_bool : uint32_t {
#define ERHE_X(PARAM) PARAM,
    ERHE_SHADER_BOOL(ERHE_X)
#undef ERHE_X
    count
};

enum class Shader_int : uint32_t {
#define ERHE_X(PARAM) PARAM,
    ERHE_SHADER_INT(ERHE_X)
#undef ERHE_X
    count
};

[[nodiscard]] inline auto make_shader_bool_mask(const Shader_bool param) -> uint32_t
{
    return uint32_t{1} << static_cast<uint32_t>(param);
}

class Shader_key
{
public:
    static_assert(
        static_cast<uint32_t>(Shader_bool::count) <= 32,
        "Variant_key: boolean axes must fit in a uint32_t bit-mask"
    );

    Shader_key();
    ~Shader_key() noexcept;

    [[nodiscard]] auto get_defines() const -> std::vector<std::pair<std::string, std::string>>;

    [[nodiscard]] auto describe() const -> std::string;

    [[nodiscard]] auto get(const Shader_bool param) const -> bool
    {
        const uint32_t bit = make_shader_bool_mask(param);
        return (bool_mask & bit) == bit;
    }
    void set(const Shader_bool param, const bool value)
    {
        const uint32_t bit = make_shader_bool_mask(param);
        if (value) {
            bool_mask |= bit;
        } else {
            bool_mask &= ~bit;
        }
    }

    [[nodiscard]] auto get(const Shader_int param) const -> uint32_t
    {
        return int_values.at(static_cast<size_t>(param));
    }
    void set(const Shader_int param, const uint32_t value)
    {
        int_values.at(static_cast<size_t>(param)) = value;
    }

    [[nodiscard]] auto operator==(const Shader_key& other) const noexcept -> bool = default;

    [[nodiscard]] auto get_hash() const -> uint64_t
    {
        constexpr std::uint64_t seed = 14695981039346656037ull;
        std::uint64_t hash = erhe::hash::hash(&bool_mask, sizeof(bool_mask), seed);
        hash = erhe::hash::hash(int_values.data(), sizeof(int_values), hash);
        if (blending_mode.has_value()) {
            hash = erhe::hash::hash(static_cast<uint8_t>(blending_mode.value()), hash);
        }
        return hash;
    }

    [[nodiscard]] auto derive(
        const erhe::primitive::Material*       material,
        const erhe::dataformat::Vertex_format* vertex_format,
        const bool                             mesh_has_skin
    ) const -> Shader_key;

    uint32_t                                                     bool_mask{0};
    std::array<uint32_t, static_cast<size_t>(Shader_int::count)> int_values;
    std::optional<erhe::primitive::Material_blending_mode>       blending_mode;
};

class Shader_key_hash
{
public:
    [[nodiscard]] auto operator()(const Shader_key& key) const noexcept -> std::size_t;
};

class Light_layer_partition
{
public:
    std::size_t per_type_shadow   [4] {0, 0, 0, 0};
    std::size_t per_type_nonshadow[4] {0, 0, 0, 0};
};

[[nodiscard]] auto compute_light_layer_partition(std::span<const std::shared_ptr<erhe::scene::Light>> lights) -> Light_layer_partition;

} // namespace erhe::scene_renderer
