#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/geometry_uploader.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/raytrace/buffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Primitive_geometry::Primitive_geometry() = default;

Primitive_geometry::~Primitive_geometry() = default;

Primitive_geometry::Primitive_geometry(Primitive_geometry&& other) noexcept
    : bounding_box_min        {other.bounding_box_min               }
    , bounding_box_max        {other.bounding_box_max               }
    , triangle_fill_indices   {other.triangle_fill_indices          }
    , edge_line_indices       {other.edge_line_indices              }
    , corner_point_indices    {other.corner_point_indices           }
    , polygon_centroid_indices{other.polygon_centroid_indices       }
    , gl_vertex_buffer        {std::move(other.gl_vertex_buffer    )}
    , gl_index_buffer         {std::move(other.gl_index_buffer     )}
    , embree_vertex_buffer    {std::move(other.embree_vertex_buffer)}
    , embree_index_buffer     {std::move(other.embree_index_buffer )}
    , vertex_byte_offset      {other.vertex_byte_offset             }
    , index_byte_offset       {other.index_byte_offset              }
    , vertex_element_size     {other.vertex_element_size            }
    , index_element_size      {other.index_element_size             }
    , vertex_count            {other.vertex_count                   }
    , index_count             {other.index_count                    }
    , source_geometry         {std::move(other.source_geometry)     }
    , source_normal_style     {other.source_normal_style            }
{
}

auto Primitive_geometry::operator=(Primitive_geometry&& other) noexcept
-> Primitive_geometry&
{
    bounding_box_min         = other.bounding_box_min               ;
    bounding_box_max         = other.bounding_box_max               ;
    triangle_fill_indices    = other.triangle_fill_indices          ;
    edge_line_indices        = other.edge_line_indices              ;
    corner_point_indices     = other.corner_point_indices           ;
    polygon_centroid_indices = other.polygon_centroid_indices       ;
    gl_vertex_buffer         = std::move(other.gl_vertex_buffer)    ;
    gl_index_buffer          = std::move(other.gl_index_buffer)     ;
    embree_vertex_buffer     = std::move(other.embree_vertex_buffer);
    embree_index_buffer      = std::move(other.embree_index_buffer) ;
    vertex_byte_offset       = other.vertex_byte_offset             ;
    index_byte_offset        = other.index_byte_offset              ;
    vertex_element_size      = other.vertex_element_size            ;
    index_element_size       = other.index_element_size             ;
    vertex_count             = other.vertex_count                   ;
    index_count              = other.index_count                    ;
    source_geometry          = std::move(other.source_geometry)     ;
    source_normal_style      = other.source_normal_style            ;
    return *this;
}

void Primitive_geometry::allocate_vertex_buffer(const Geometry_uploader& uploader,
                                                const size_t             vertex_count,
                                                const size_t             vertex_element_size)
{
    this->vertex_count        = vertex_count;
    this->vertex_element_size = vertex_element_size;

    if (uploader.buffer_info.gl_vertex_buffer)
    {
        this->gl_vertex_buffer = uploader.buffer_info.gl_vertex_buffer;
    }
    else
    {
        constexpr gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit;
        this->gl_vertex_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::array_buffer,
                                                                          vertex_count * vertex_element_size,
                                                                          storage_mask);
    }

    this->vertex_byte_offset = gl_vertex_buffer->allocate_bytes(vertex_count * vertex_element_size,
                                                                vertex_element_size);
}

void Primitive_geometry::allocate_index_buffer(const Geometry_uploader& uploader,
                                               const size_t             index_count,
                                               const size_t             index_element_size)
{
    this->index_count        = index_count;
    this->index_element_size = index_element_size;
    if (uploader.buffer_info.gl_index_buffer)
    {
        this->gl_index_buffer = uploader.buffer_info.gl_index_buffer;
    }
    else
    {
        constexpr gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit;
        this->gl_index_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::element_array_buffer,
                                                                         index_count * index_element_size,
                                                                         storage_mask);
    }
    this->index_byte_offset = gl_index_buffer->allocate_bytes(index_count * index_element_size);
}

auto Primitive_geometry::index_range(Primitive_mode primitive_mode) const -> Index_range
{
    switch (primitive_mode)
    {
        case Primitive_mode::not_set          : return {};
        case Primitive_mode::polygon_fill     : return triangle_fill_indices;
        case Primitive_mode::edge_lines       : return edge_line_indices;
        case Primitive_mode::corner_points    : return corner_point_indices;
        case Primitive_mode::polygon_centroids: return polygon_centroid_indices;
        case Primitive_mode::count            : return {};
        default:                                return {};
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
