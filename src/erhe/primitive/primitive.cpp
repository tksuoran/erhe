#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Primitive::Primitive() = default;


Primitive::Primitive(
    std::shared_ptr<Primitive_geometry> primitive_geometry,
    std::shared_ptr<Material>           material
)
    : primitive_geometry{primitive_geometry}
    , material         {material}
{
}

auto c_str(Primitive_mode primitive_mode) -> const char*
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

auto c_str(Normal_style normal_style) -> const char*
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

} // namespace erhe::primitive
