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
        none           =    0,
        automatic      =    1,
        position       =    2,
        tangent        =    4,
        normal         =    8,
        bitangent      =   16,
        color          =   32,
        weights        =   64,
        matrix_indices =  128,
        tex_coord      =  256,
        id             =  512,
        custom         = 1024
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
