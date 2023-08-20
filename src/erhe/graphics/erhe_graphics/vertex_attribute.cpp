#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics
{

auto Vertex_attribute::Usage::operator==(const Usage& other) const -> bool
{
    // TODO index is not compared. Is this a bug or design?
    return (type == other.type);
}

auto Vertex_attribute::Usage::operator!=(const Usage& other) const -> bool
{
    return !(*this == other);
}

auto Vertex_attribute::Data_type::operator==(const Data_type& other) const -> bool
{
    return
        (type       == other.type)       &&
        (normalized == other.normalized) &&
        (dimension  == other.dimension);
}

auto Vertex_attribute::Data_type::operator!=(const Data_type& other) const -> bool
{
    return !(*this == other);
}

auto Vertex_attribute::size() const -> size_t
{
    return data_type.dimension * gl_helpers::size_of_type(data_type.type);
}

auto Vertex_attribute::operator==(const Vertex_attribute& other) const -> bool
{
    return
        (usage       == other.usage      ) &&
        (data_type   == other.data_type  ) &&
        (shader_type == other.shader_type) &&
        (offset      == other.offset     ) &&
        (divisor     == other.divisor);
}

auto Vertex_attribute::operator!=(const Vertex_attribute& other
) const -> bool
{
    return !(*this == other);
}

auto Vertex_attribute::desc(const Usage_type usage) -> const char*
{
    if (usage == (Usage_type::position | Usage_type::tex_coord)) {
        return "position | tex_coord";
    }

    switch (usage) {
        //using enum Usage_type;
        case Usage_type::none:          return "none";
        case Usage_type::position:      return "position";
        case Usage_type::tangent:       return "tangent";
        case Usage_type::bitangent:     return "bitangent";
        case Usage_type::normal:        return "normal";
        case Usage_type::color:         return "color";
        case Usage_type::joint_weights: return "joint_weights";
        case Usage_type::joint_indices: return "joint_indices";
        case Usage_type::tex_coord:     return "tex_coord";
        case Usage_type::id:            return "id";
        case Usage_type::material:      return "material";
        case Usage_type::aniso_control: return "aniso_control";
        case Usage_type::custom:        return "custom";
        default:                        return "?";
    }
}

} // namespace erhe::graphics
