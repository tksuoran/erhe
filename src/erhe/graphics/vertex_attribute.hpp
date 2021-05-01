#ifndef vertex_attribute_hpp_erhe_graphics
#define vertex_attribute_hpp_erhe_graphics

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
        position       =   1,
        tangent        =   2,
        normal         =   4,
        bitangent      =   8,
        color          =  16,
        weights        =  32,
        matrix_indices =  64,
        tex_coord      = 128,
        id             = 256
    };

    struct Usage
    {
        Usage(Usage_type type, size_t index)
            : type {type}
            , index{index}
        {
        }

        auto operator==(const Usage& other) const
        -> bool
        {
            // TODO index is not compared. Is this a bug or design?
            return (type == other.type);
        }

        auto operator!=(const Usage& other) const
        -> bool
        {
            return !(*this == other);
        }

        Usage_type type {Usage_type::none};
        size_t     index{0};
    };

    struct Data_type
    {
        Data_type(gl::Vertex_attrib_type type,
                  bool                   normalized,
                  size_t                 dimension)
            : type      {type}
            , normalized{normalized}
            , dimension {dimension}
        {
        }

        auto operator==(const Data_type& other) const
        -> bool
        {
            return (type == other.type)             &&
                   (normalized == other.normalized) &&
                   (dimension == other.dimension);
        }

        auto operator!=(const Data_type& other) const
        -> bool
        {
            return !(*this == other);
        }

        gl::Vertex_attrib_type type;
        bool                   normalized{false};
        size_t                 dimension {0};
    };

    static auto desc(Usage_type usage)
    -> const char*;

    Vertex_attribute(Usage              usage,
                     gl::Attribute_type shader_type,
                     Data_type          data_type,
                     size_t             offset  = 0,
                     unsigned int       divisor = 0)
        : usage      {usage}
        , shader_type{shader_type}
        , data_type  {data_type}
        , offset     {offset}
        , divisor    {divisor}
    {
    }

    auto stride() const
    -> size_t
    {
        return data_type.dimension * gl::size_of_type(data_type.type);
    }

    auto operator==(const Vertex_attribute& other) const
    -> bool
    {
        return (usage       == other.usage      ) &&
               (data_type   == other.data_type  ) &&
               (shader_type == other.shader_type) &&
               (offset      == other.offset     ) &&
               (divisor     == other.divisor);
    }

    auto operator!=(const Vertex_attribute& other) const
    -> bool
    {
        return !(*this == other);
    }

    Usage                  usage;
    gl::Attribute_type     shader_type;
    Data_type              data_type;
    size_t                 offset    {0};
    unsigned int           divisor   {0};
};

template<typename Enum>
struct Enable_bit_mask_operators
{
    static const bool enable = false;
};

template<typename Enum>
constexpr auto
operator |(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
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

#endif
