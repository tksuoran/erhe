#include "erhe_primitive/index_range.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::primitive {

auto Index_range::get_primitive_count() const -> std::size_t
{
    return index_count > 2 ? index_count / 3 : 0;
    switch (primitive_type) {
        case Primitive_type::line_loop               : return index_count > 1 ? index_count : 0;
        case Primitive_type::line_strip              : return index_count > 1 ? index_count - 1 : 0;
        case Primitive_type::line_strip_adjacency    : ERHE_FATAL("TODO");
        case Primitive_type::lines                   : return index_count > 1 ? index_count / 2 : 0;
        case Primitive_type::lines_adjacency         : ERHE_FATAL("TODO");
        case Primitive_type::patches                 : ERHE_FATAL("TODO");
        case Primitive_type::points                  : return index_count;
        case Primitive_type::quads                   : ERHE_FATAL("TODO");
        case Primitive_type::triangle_fan            : return index_count > 2 ? index_count - 2 : 0;
        case Primitive_type::triangle_strip          : return index_count > 2 ? index_count - 2 : 0;
        case Primitive_type::triangle_strip_adjacency: ERHE_FATAL("TODO");
        case Primitive_type::triangles               : return index_count > 2 ? index_count / 3 : 0;
        case Primitive_type::triangles_adjacency     : ERHE_FATAL("TODO");
        default: ERHE_FATAL("Unsupported primitive type");
    }
}

auto Index_range::get_point_count() const -> std::size_t
{
    switch (primitive_type) {
        case Primitive_type::line_loop               : return 0;
        case Primitive_type::line_strip              : return 0;
        case Primitive_type::line_strip_adjacency    : return 0;
        case Primitive_type::lines                   : return 0;
        case Primitive_type::lines_adjacency         : return 0;
        case Primitive_type::patches                 : return 0;
        case Primitive_type::points                  : return index_count;
        case Primitive_type::quads                   : return 0;
        case Primitive_type::triangle_fan            : return 0;
        case Primitive_type::triangle_strip          : return 0;
        case Primitive_type::triangle_strip_adjacency: return 0;
        case Primitive_type::triangles               : return 0;
        case Primitive_type::triangles_adjacency     : return 0;
        default: ERHE_FATAL("Unsupported primitive type");
    }
}

auto Index_range::get_line_count() const -> std::size_t
{
    switch (primitive_type) {
        case Primitive_type::line_loop               : return index_count > 1 ? index_count : 0;
        case Primitive_type::line_strip              : return index_count > 1 ? index_count - 1 : 0;
        case Primitive_type::line_strip_adjacency    : ERHE_FATAL("TODO");
        case Primitive_type::lines                   : return index_count > 1 ? index_count / 2 : 0;
        case Primitive_type::lines_adjacency         : ERHE_FATAL("TODO");
        case Primitive_type::patches                 : return 0;
        case Primitive_type::points                  : return 0;
        case Primitive_type::quads                   : return 0;
        case Primitive_type::triangle_fan            : return 0;
        case Primitive_type::triangle_strip          : return 0;
        case Primitive_type::triangle_strip_adjacency: return 0;
        case Primitive_type::triangles               : return 0;
        case Primitive_type::triangles_adjacency     : return 0;
        default: ERHE_FATAL("Unsupported primitive type");
    }
}

auto Index_range::get_triangle_count() const -> std::size_t
{
    switch (primitive_type) {
        case Primitive_type::line_loop               : return 0;
        case Primitive_type::line_strip              : return 0;
        case Primitive_type::line_strip_adjacency    : return 0;
        case Primitive_type::lines                   : return 0;
        case Primitive_type::lines_adjacency         : return 0;
        case Primitive_type::patches                 : return 0;
        case Primitive_type::points                  : return 0;
        case Primitive_type::quads                   : return 0;
        case Primitive_type::triangle_fan            : return index_count > 2 ? index_count - 2 : 0;
        case Primitive_type::triangle_strip          : return index_count > 2 ? index_count - 2 : 0;
        case Primitive_type::triangle_strip_adjacency: ERHE_FATAL("TODO");
        case Primitive_type::triangles               : return index_count > 2 ? index_count / 3 : 0;
        case Primitive_type::triangles_adjacency     : ERHE_FATAL("TODO");
        default: ERHE_FATAL("Unsupported primitive type");
    }
}

} // namespace erhe::primitive
