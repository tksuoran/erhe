#include "erhe_primitive/renderable_mesh.hpp"

namespace erhe::primitive
{

auto Renderable_mesh::base_vertex() const -> uint32_t
{
    return static_cast<uint32_t>(vertex_buffer_range.byte_offset / vertex_buffer_range.element_size);
}

// Value that should be added in index range first index
auto Renderable_mesh::base_index() const -> uint32_t
{
    return static_cast<uint32_t>(index_buffer_range.byte_offset / index_buffer_range.element_size);
}

auto Renderable_mesh::index_range(const Primitive_mode primitive_mode) const -> Index_range
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set          : return {};
        case Primitive_mode::polygon_fill     : return triangle_fill_indices;
        case Primitive_mode::edge_lines       : return edge_line_indices;
        case Primitive_mode::corner_points    : return corner_point_indices;
        case Primitive_mode::polygon_centroids: return polygon_centroid_indices;
        case Primitive_mode::count            : return {};
        default:                                return {};
    }
}

auto primitive_type(
    const Primitive_mode primitive_mode
) -> std::optional<gl::Primitive_type>
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set          : return {};
        case Primitive_mode::polygon_fill     : return gl::Primitive_type::triangles;
        case Primitive_mode::edge_lines       : return gl::Primitive_type::lines;
        case Primitive_mode::corner_points    : return gl::Primitive_type::points;
        case Primitive_mode::polygon_centroids: return gl::Primitive_type::points;
        case Primitive_mode::count            : return {};
        default:                                return {};
    }
}

} // namespace erhe::primitive
