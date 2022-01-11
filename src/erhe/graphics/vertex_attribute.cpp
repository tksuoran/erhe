#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::graphics
{

auto Vertex_attribute::desc(const Usage_type usage)
-> const char*
{
    if (usage == (Usage_type::position | Usage_type::tex_coord))
    {
        return "position | tex_coord";
    }

    switch (usage)
    {
        using enum Usage_type;
        case none:           return "none";
        case position:       return "position";
        case tangent:        return "tangent";
        case normal:         return "normal";
        case bitangent:      return "bitangent";
        case color:          return "color";
        case weights:        return "weights";
        case matrix_indices: return "matrix_indices";
        case tex_coord:      return "tex_coord";
        case id:             return "id";
        default:
        {
            ERHE_FATAL("Bad vertex attribute usage\n");
        }
    }
}

} // namespace erhe::graphics
