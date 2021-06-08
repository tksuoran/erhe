#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Primitive_geometry::Primitive_geometry() = default;

Primitive_geometry::~Primitive_geometry() = default;

Primitive_geometry::Primitive_geometry(Primitive_geometry&& other) noexcept
    : bounding_box_min        {other.bounding_box_min          }
    , bounding_box_max        {other.bounding_box_max          }
    , fill_indices            {other.fill_indices              }
    , edge_line_indices       {other.edge_line_indices         }
    , corner_point_indices    {other.corner_point_indices      }
    , polygon_centroid_indices{other.polygon_centroid_indices  }
    , vertex_buffer           {std::move(other.vertex_buffer)  }
    , index_buffer            {std::move(other.index_buffer)   }
    , vertex_byte_offset      {other.vertex_byte_offset        }
    , index_byte_offset       {other.index_byte_offset         }
    , vertex_element_size     {other.vertex_element_size       }
    , index_element_size      {other.index_element_size        }
    , vertex_count            {other.vertex_count              }
    , index_count             {other.index_count               }
    , source_geometry         {std::move(other.source_geometry)}
    , source_normal_style     {other.source_normal_style       }
{
}

auto Primitive_geometry::operator=(Primitive_geometry&& other) noexcept
-> Primitive_geometry&
{
    bounding_box_min         = other.bounding_box_min          ;
    bounding_box_max         = other.bounding_box_max          ;
    fill_indices             = other.fill_indices              ;
    edge_line_indices        = other.edge_line_indices         ;
    corner_point_indices     = other.corner_point_indices      ;
    polygon_centroid_indices = other.polygon_centroid_indices  ;
    vertex_buffer            = std::move(other.vertex_buffer)  ;
    index_buffer             = std::move(other.index_buffer)   ;
    vertex_byte_offset       = other.vertex_byte_offset        ;
    index_byte_offset        = other.index_byte_offset         ;
    vertex_element_size      = other.vertex_element_size       ;
    index_element_size       = other.index_element_size        ;
    vertex_count             = other.vertex_count              ;
    index_count              = other.index_count               ;
    source_geometry          = std::move(other.source_geometry);
    source_normal_style      = other.source_normal_style       ;
    return *this;
}

Primitive::Primitive() = default;


Primitive::Primitive(std::shared_ptr<Primitive_geometry> primitive_geometry,
                     std::shared_ptr<Material>           material)
    : primitive_geometry{primitive_geometry}
    , material          {material}
{
}

auto c_str(Primitive_mode primitive_mode)
-> const char*
{
    switch (primitive_mode)
    {
        case Primitive_mode::not_set:           return "not_set";
        case Primitive_mode::polygon_fill:      return "polygon_fill";
        case Primitive_mode::edge_lines:        return "edge_lines";
        case Primitive_mode::corner_points:     return "corner_points";
        case Primitive_mode::corner_normals:    return "corner_normals";
        case Primitive_mode::polygon_centroids: return "polygon_centroids";
        case Primitive_mode::count:             return "count";
        default:
        {
            FATAL("Bad mesh mode");
        }
    }
}

auto c_str(Normal_style normal_style)
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

auto Primitive_geometry::index_range(Primitive_mode primitive_mode) const -> Index_range
{
    switch (primitive_mode)
    {
        case Primitive_mode::not_set          : return {};
        case Primitive_mode::polygon_fill     : return fill_indices;
        case Primitive_mode::edge_lines       : return edge_line_indices;
        case Primitive_mode::corner_points    : return corner_point_indices;
        case Primitive_mode::polygon_centroids: return polygon_centroid_indices;
        case Primitive_mode::count            : return {};
        default:                      return {};
    }
}

auto primitive_type(Primitive_mode primitive_mode)
-> std::optional<gl::Primitive_type>
{
    switch (primitive_mode)
    {
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
