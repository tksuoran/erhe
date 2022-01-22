#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/gl/gl.hpp"

#include <cstddef>
#include <type_traits>

namespace erhe::graphics
{

class Vertex_attribute
{
public:
    enum class Usage_type : unsigned int
    {
        none           =   0,
        automatic      =   1,
        position       =   2,
        tangent        =   4,
        normal         =   8,
        bitangent      =  16,
        color          =  32,
        weights        =  64,
        matrix_indices = 128,
        tex_coord      = 256,
        id             = 512
    };

    // for example tex_coord 0
    class Usage
    {
    public:
        [[nodiscard]] auto operator==(const Usage& other) const -> bool
        {
            // TODO index is not compared. Is this a bug or design?
            return (type == other.type);
        }

        [[nodiscard]] auto operator!=(const Usage& other) const -> bool
        {
            return !(*this == other);
        }

        Usage_type type {Usage_type::none};
        size_t     index{0};
    };

    // type, normalized, dimension -> dvec3 for example is double, false, 3
    class Data_type
    {
    public:
        [[nodiscard]] auto operator==(const Data_type& other) const -> bool
        {
            return
                (type       == other.type)       &&
                (normalized == other.normalized) &&
                (dimension  == other.dimension);
        }

        [[nodiscard]] auto operator!=(const Data_type& other) const -> bool
        {
            return !(*this == other);
        }

        gl::Vertex_attrib_type type;
        bool                   normalized{false};
        size_t                 dimension {0};
    };

    [[nodiscard]] static auto desc(const Usage_type usage) -> const char*;

    [[nodiscard]] auto stride() const -> size_t
    {
        return data_type.dimension * gl::size_of_type(data_type.type);
    }

    [[nodiscard]] auto operator==(
        const Vertex_attribute& other
    ) const -> bool
    {
        return
            (usage       == other.usage      ) &&
            (data_type   == other.data_type  ) &&
            (shader_type == other.shader_type) &&
            (offset      == other.offset     ) &&
            (divisor     == other.divisor);
    }

    [[nodiscard]] auto operator!=(
        const Vertex_attribute& other
    ) const -> bool
    {
        return !(*this == other);
    }

    Usage              usage;
    gl::Attribute_type shader_type;
    Data_type          data_type;
    size_t             offset {0};
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
