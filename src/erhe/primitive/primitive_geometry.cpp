#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Primitive_geometry::Primitive_geometry() = default;

Primitive_geometry::~Primitive_geometry() = default;

Primitive_geometry::Primitive_geometry(Primitive_geometry&& other) noexcept
    : bounding_box_min        {other.bounding_box_min          }
    , bounding_box_max        {other.bounding_box_max          }
    , triangle_fill_indices   {other.triangle_fill_indices     }
    , edge_line_indices       {other.edge_line_indices         }
    , corner_point_indices    {other.corner_point_indices      }
    , polygon_centroid_indices{other.polygon_centroid_indices  }
    , vertex_buffer_range     {other.vertex_buffer_range       }
    , index_buffer_range      {other.index_buffer_range        }
    , source_geometry         {std::move(other.source_geometry)}
    , source_normal_style     {other.source_normal_style       }
{
}

auto Primitive_geometry::operator=(Primitive_geometry&& other) noexcept
-> Primitive_geometry&
{
    bounding_box_min         = other.bounding_box_min          ;
    bounding_box_max         = other.bounding_box_max          ;
    triangle_fill_indices    = other.triangle_fill_indices     ;
    edge_line_indices        = other.edge_line_indices         ;
    corner_point_indices     = other.corner_point_indices      ;
    polygon_centroid_indices = other.polygon_centroid_indices  ;
    vertex_buffer_range      = other.vertex_buffer_range       ;
    index_buffer_range       = other.index_buffer_range        ;
    source_geometry          = std::move(other.source_geometry);
    source_normal_style      = other.source_normal_style       ;
    return *this;
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
