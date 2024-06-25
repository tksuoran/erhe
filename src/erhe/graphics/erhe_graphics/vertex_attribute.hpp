#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <type_traits>

namespace erhe::graphics
{

enum class Glsl_type
{
    invalid = 0,
    float_,
    float_vec2,
    float_vec3,
    float_vec4,
    bool_,
    int_,
    int_vec2,
    int_vec3,
    int_vec4,
    unsigned_int,
    unsigned_int_vec2,
    unsigned_int_vec3,
    unsigned_int_vec4,
    unsigned_int64_arb,
    float_mat_2x2,
    float_mat_3x3,
    float_mat_4x4,
};

[[nodiscard]] auto c_str(Glsl_type value) -> const char*;

class Vertex_attribute
{
public:
    enum class Usage_type : unsigned int {
        none          =  0,
        automatic     = (1 <<  0u),
        position      = (1 <<  1u),
        tangent       = (1 <<  2u),
        bitangent     = (1 <<  3u),
        normal        = (1 <<  4u),
        color         = (1 <<  5u),
        joint_indices = (1 <<  6u),
        joint_weights = (1 <<  7u),
        tex_coord     = (1 <<  8u),
        id            = (1 <<  9u),
        material      = (1 << 10u), // metallic roughess_x roughness_y
        aniso_control = (1 << 11u), // anisotropy tangent_space
        custom        = (1 << 12u)
    };

    // for example tex_coord 0
    class Usage
    {
    public:
        [[nodiscard]] auto operator==(const Usage& other) const -> bool;
        [[nodiscard]] auto operator!=(const Usage& other) const -> bool;

        Usage_type  type {Usage_type::none};
        std::size_t index{0};
    };

    [[nodiscard]] static auto desc(Usage_type usage) -> const char*;

    [[nodiscard]] auto size      () const -> std::size_t;
    [[nodiscard]] auto operator==(const Vertex_attribute& other) const -> bool;
    [[nodiscard]] auto operator!=(const Vertex_attribute& other) const -> bool;

    std::string              name         {};
    Usage                    usage        {};
    Glsl_type                shader_type  {Glsl_type::invalid};
    erhe::dataformat::Format data_type    {erhe::dataformat::Format::format_undefined};
    std::size_t              offset       {0};
    unsigned int             divisor      {0};
    glm::vec4                default_value{0.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] static auto position_float2() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::position },
            .shader_type = Glsl_type::float_vec2,
            .data_type   = erhe::dataformat::Format::format_32_vec2_float
        };
    }
    [[nodiscard]] static auto position_float3() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::position },
            .shader_type = Glsl_type::float_vec3,
            .data_type   = erhe::dataformat::Format::format_32_vec3_float
        };
    }
    [[nodiscard]] static auto position0_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::position },
            .shader_type = Glsl_type::float_vec4,
            .data_type   = erhe::dataformat::Format::format_32_vec4_float
        };
    }
    [[nodiscard]] static auto position1_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::position, 1 },
            .shader_type = Glsl_type::float_vec4,
            .data_type   = erhe::dataformat::Format::format_32_vec4_float
        };
    }
    [[nodiscard]] static auto position2_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::position, 2 },
            .shader_type = Glsl_type::float_vec4,
            .data_type   = erhe::dataformat::Format::format_32_vec4_float
        };
    }
    [[nodiscard]] static auto normal0_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = {Usage_type::normal },
            .shader_type   = Glsl_type::float_vec3,
            .data_type     = erhe::dataformat::Format::format_32_vec3_float,
            .default_value = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
        };
    }
    [[nodiscard]] static auto normal1_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::normal },
            .shader_type = Glsl_type::float_vec3,
            .data_type   = erhe::dataformat::Format::format_32_vec3_float,
            .default_value = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
        };
    }
    [[nodiscard]] static auto tangent_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = { Usage_type::tangent },
            .shader_type   = Glsl_type::float_vec3,
            .data_type     = erhe::dataformat::Format::format_32_vec3_float,
            .default_value = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}
        };
    }
    [[nodiscard]] static auto tangent_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = { Usage_type::tangent },
            .shader_type   = Glsl_type::float_vec4,
            .data_type     = erhe::dataformat::Format::format_32_vec4_float,
            .default_value = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}
        };
    }
    [[nodiscard]] static auto bitangent_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = { Usage_type::bitangent },
            .shader_type   = Glsl_type::float_vec3,
            .data_type     = erhe::dataformat::Format::format_32_vec3_float,
            .default_value = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}
        };
    }
    [[nodiscard]] static auto texcoord0_float2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::tex_coord },
            .shader_type = Glsl_type::float_vec2,
            .data_type   = erhe::dataformat::Format::format_32_vec2_float
        };
    }
    [[nodiscard]] static auto texcoord1_float2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::tex_coord, 1 },
            .shader_type = Glsl_type::float_vec2,
            .data_type   = erhe::dataformat::Format::format_32_vec2_float
        };
    }
    [[nodiscard]] static auto color_ubyte4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = { Usage_type::color },
            .shader_type   = Glsl_type::float_vec4,
            .data_type     = erhe::dataformat::Format::format_8_vec4_unorm,
            .default_value = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
        };
    }
    [[nodiscard]] static auto color_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage         = { Usage_type::color },
            .shader_type   = Glsl_type::float_vec4,
            .data_type     = erhe::dataformat::Format::format_32_vec4_float,
            .default_value = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
        };
    }
    [[nodiscard]] static auto aniso_control_ubyte2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::aniso_control },
            .shader_type = Glsl_type::float_vec2,
            .data_type   = erhe::dataformat::Format::format_8_vec2_unorm
        };
    }
    [[nodiscard]] static auto joint_indices0_ubyte4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::joint_indices },
            .shader_type = Glsl_type::int_vec4,
            .data_type   = erhe::dataformat::Format::format_8_vec4_uint
        };
    }
    [[nodiscard]] static auto joint_weights0_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage       = { Usage_type::joint_weights },
            .shader_type = Glsl_type::float_vec4,
            .data_type   = erhe::dataformat::Format::format_32_vec4_float
        };
    }
};

template<typename Enum>
struct Enable_bit_mask_operators
{
    static const bool enable = false;
};

template<typename Enum>
constexpr auto operator |(
    const Enum lhs,
    const Enum rhs
) -> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template<>
struct Enable_bit_mask_operators<erhe::graphics::Vertex_attribute::Usage_type>
{
    static const bool enable = true;
};

} // namespace erhe::graphics
