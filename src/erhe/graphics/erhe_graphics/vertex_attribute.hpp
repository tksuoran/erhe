#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <cstddef>
#include <type_traits>

namespace erhe::graphics
{

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

    // type, normalized, dimension -> dvec3 for example is double, false, 3
    class Data_type
    {
    public:
        [[nodiscard]] auto operator==(const Data_type& other) const -> bool;
        [[nodiscard]] auto operator!=(const Data_type& other) const -> bool;

        gl::Vertex_attrib_type type      {gl::Vertex_attrib_type::float_};
        bool                   normalized{false};
        std::size_t            dimension {0};
    };

    [[nodiscard]] static auto desc(Usage_type usage) -> const char*;

    [[nodiscard]] auto size      () const -> std::size_t;
    [[nodiscard]] auto operator==(const Vertex_attribute& other) const -> bool;
    [[nodiscard]] auto operator!=(const Vertex_attribute& other) const -> bool;

    Usage              usage      {};
    gl::Attribute_type shader_type{gl::Attribute_type::float_};
    Data_type          data_type  {};
    std::size_t        offset     {0};
    unsigned int       divisor    {0};

    [[nodiscard]] static auto position_float2() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        };
    }
    [[nodiscard]] static auto position_float3() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        };
    }
    [[nodiscard]] static auto position0_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::position,
                .index     = 0
            },
            .shader_type   = gl::Attribute_type::float_vec4,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 4
            }
        };
    }
    [[nodiscard]] static auto position1_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::position,
                .index     = 1
            },
            .shader_type   = gl::Attribute_type::float_vec4,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 4
            }
        };
    }
    [[nodiscard]] static auto position2_float4() -> erhe::graphics::Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::position,
                .index     = 2
            },
            .shader_type   = gl::Attribute_type::float_vec4,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 4
            }
        };
    }
    [[nodiscard]] static auto normal0_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::normal,
                .index     = 0
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        };
    }
    [[nodiscard]] static auto normal1_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::normal,
                .index     = 1
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        };
    }
    [[nodiscard]] static auto tangent_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::tangent
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        };
    }
    [[nodiscard]] static auto tangent_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::tangent
            },
            .shader_type   = gl::Attribute_type::float_vec4,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 4
            }
        };
    }
    [[nodiscard]] static auto bitangent_float3() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::bitangent
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        };
    }
    [[nodiscard]] static auto texcoord0_float2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::tex_coord,
                .index     = 0
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        };
    }
    [[nodiscard]] static auto texcoord1_float2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type      = Usage_type::tex_coord,
                .index     = 1
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        };
    }
    [[nodiscard]] static auto color_ubyte4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type       = Usage_type::color
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type = {
                .type       = gl::Vertex_attrib_type::unsigned_byte,
                .normalized = true,
                .dimension  = 4
            }
        };
    }
    [[nodiscard]] static auto color_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type       = Usage_type::color
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type = {
                .type       = gl::Vertex_attrib_type::float_,
                .dimension  = 4
            }
        };
    }
    [[nodiscard]] static auto aniso_control_ubyte2() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type       = Usage_type::aniso_control
            },
            .shader_type    = gl::Attribute_type::float_vec2,
            .data_type = {
                .type       = gl::Vertex_attrib_type::unsigned_byte,
                .normalized = true,
                .dimension  = 2
            }
        };
    }
    [[nodiscard]] static auto joint_indices0_ubyte4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type       = Usage_type::joint_indices,
                .index      = 0

            },
            .shader_type    = gl::Attribute_type::unsigned_int_vec4,
            .data_type = {
                .type       = gl::Vertex_attrib_type::unsigned_byte,
                .normalized = true,
                .dimension  = 4
            }
        };
    }
    [[nodiscard]] static auto joint_weights0_float4() -> Vertex_attribute
    {
        return Vertex_attribute{
            .usage = {
                .type       = Usage_type::joint_weights,
                .index      = 0

            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type = {
                .type       = gl::Vertex_attrib_type::float_,
                .dimension  = 4
            }
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
