#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::graphics
{

auto Vertex_attribute::desc(
    const Usage_type usage
) -> const char*
{
    if (usage == (Usage_type::position | Usage_type::tex_coord))
    {
        return "position | tex_coord";
    }

    switch (usage)
    {
        //using enum Usage_type;
        case Usage_type::none:           return "none";
        case Usage_type::position:       return "position";
        case Usage_type::tangent:        return "tangent";
        case Usage_type::normal:         return "normal";
        case Usage_type::bitangent:      return "bitangent";
        case Usage_type::color:          return "color";
        case Usage_type::weights:        return "weights";
        case Usage_type::matrix_indices: return "matrix_indices";
        case Usage_type::tex_coord:      return "tex_coord";
        case Usage_type::id:             return "id";
        case Usage_type::custom:         return "custom";
        default:
        {
            ERHE_FATAL("Bad vertex attribute usage\n");
        }
    }
}

} // namespace erhe::graphics
