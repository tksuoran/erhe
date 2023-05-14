#pragma once

#include "erhe/gl/wrapper_enums.hpp"

#include <cstddef>
#include <type_traits>

namespace erhe::graphics
{

class Vertex_attribute
{
public:
    enum class Usage_type : unsigned int
    {
        none           =  0,
        automatic      = (1 <<  0u),
        position       = (1 <<  1u),
        tangent        = (1 <<  2u),
        normal         = (1 <<  3u),
        bitangent      = (1 <<  4u),
        color          = (1 <<  5u),
        weights        = (1 <<  6u),
        matrix_indices = (1 <<  7u),
        tex_coord      = (1 <<  8u),
        id             = (1 <<  9u),
        custom         = (1 << 10u)
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

        gl::Vertex_attrib_type type;
        bool                   normalized{false};
        std::size_t            dimension {0};
    };

    [[nodiscard]] static auto desc(Usage_type usage) -> const char*;

    [[nodiscard]] auto stride    () const -> size_t;
    [[nodiscard]] auto operator==(const Vertex_attribute& other) const -> bool;
    [[nodiscard]] auto operator!=(const Vertex_attribute& other) const -> bool;

    Usage              usage;
    gl::Attribute_type shader_type;
    Data_type          data_type;
    std::size_t        offset {0};
    unsigned int       divisor{0};
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
