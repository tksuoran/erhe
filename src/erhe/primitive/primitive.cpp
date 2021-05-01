#include "erhe/primitive/primitive.hpp"
#include "erhe/graphics/buffer.hpp"

namespace erhe::primitive
{

auto Primitive_geometry::desc(Mode mode)
-> const char*
{
    switch (mode)
    {
        case Mode::not_set:           return "not_set";
        case Mode::polygon_fill:      return "polygon_fill";
        case Mode::edge_lines:        return "edge_lines";
        case Mode::corner_points:     return "corner_points";
        case Mode::corner_normals:    return "corner_normals";
        case Mode::polygon_centroids: return "polygon_centroids";
        case Mode::count:             return "count";
        default:
        {
            FATAL("Bad mesh mode");
        }
    }
}

auto Primitive_geometry::desc(Normal_style normal_style)
-> const char*
{
    switch (normal_style)
    {
        case Normal_style::none:            return "none";
        case Normal_style::corner_normals:  return "corner_normals";
        case Normal_style::polygon_normals: return "polygon_normals";
        case Normal_style::point_normals:   return "point_normals";
        default:
        {
            FATAL("Bad mesh normal style");
        }
    }
}

void Primitive_geometry::allocate_vertex_buffer(size_t                  vertex_count,
                                                size_t                  vertex_element_size,
                                                gl::Buffer_storage_mask storage_mask)
{
    vertex_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::array_buffer,
                                                             vertex_count * vertex_element_size,
                                                             storage_mask);

    this->vertex_count        = vertex_count;
    this->vertex_byte_offset  = vertex_buffer->allocate_bytes(vertex_count * vertex_element_size);
    this->vertex_element_size = vertex_element_size;
}

void Primitive_geometry::allocate_vertex_buffer(const std::shared_ptr<erhe::graphics::Buffer>& buffer,
                                                size_t                                         vertex_count,
                                                size_t                                         vertex_element_size)
{
    this->vertex_buffer       = buffer;
    this->vertex_count        = vertex_count;
    this->vertex_byte_offset  = vertex_buffer->allocate_bytes(vertex_count * vertex_element_size, vertex_element_size);
    this->vertex_element_size = vertex_element_size;
}

void Primitive_geometry::allocate_index_buffer(size_t                  index_count,
                                               size_t                  index_element_size,
                                               gl::Buffer_storage_mask storage_mask)
{
    index_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::element_array_buffer,
                                                            index_count * index_element_size,
                                                            storage_mask);

    this->index_count        = index_count;
    this->index_byte_offset  = index_buffer->allocate_bytes(index_count * index_element_size);
    this->index_element_size = index_element_size;
}

void Primitive_geometry::allocate_index_buffer(const std::shared_ptr<erhe::graphics::Buffer>& buffer,
                                               size_t                                         index_count,
                                               size_t                                         index_element_size)
{
    this->index_buffer       = buffer;
    this->index_count        = index_count;
    this->index_byte_offset  = index_buffer->allocate_bytes(index_count * index_element_size);
    this->index_element_size = index_element_size;
}

auto Primitive_geometry::index_range(Mode mode) -> Index_range
{
    switch (mode)
    {
        case Mode::not_set          : return {};
        case Mode::polygon_fill     : return fill_indices;
        case Mode::edge_lines       : return edge_line_indices;
        case Mode::corner_points    : return corner_point_indices;
        case Mode::polygon_centroids: return polygon_centroid_indices;
        case Mode::count            : return {};
        default:                      return {};
    }
}

std::optional<gl::Primitive_type> primitive_type(Primitive_geometry::Mode mode)
{
    switch (mode)
    {
        case Primitive_geometry::Mode::not_set          : return {};
        case Primitive_geometry::Mode::polygon_fill     : return gl::Primitive_type::triangles;
        case Primitive_geometry::Mode::edge_lines       : return gl::Primitive_type::lines;
        case Primitive_geometry::Mode::corner_points    : return gl::Primitive_type::points;
        case Primitive_geometry::Mode::polygon_centroids: return gl::Primitive_type::points;
        case Primitive_geometry::Mode::count            : return {};
        default:                                          return {};
    }
}

} // namespace erhe::primitive
