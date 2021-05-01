#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/log/log.hpp"
#include <stdexcept>

namespace erhe::graphics
{

auto Vertex_attribute::desc(Usage_type usage)
-> const char*
{
    if (usage == (Usage_type::position | Usage_type::tex_coord))
    {
        return "position | tex_coord";
    }

    switch (usage)
    {
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
        default:
            FATAL("Bad vertex attribute usage");
    }
}

} // namespace erhe::graphics
