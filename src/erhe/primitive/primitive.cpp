#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

auto c_str(const Primitive_mode primitive_mode) -> const char*
{
    switch (primitive_mode)
    {
        //using enum Primitive_mode;
        case Primitive_mode::not_set:           return "not_set";
        case Primitive_mode::polygon_fill:      return "polygon_fill";
        case Primitive_mode::edge_lines:        return "edge_lines";
        case Primitive_mode::corner_points:     return "corner_points";
        case Primitive_mode::corner_normals:    return "corner_normals";
        case Primitive_mode::polygon_centroids: return "polygon_centroids";
        case Primitive_mode::count:             return "count";
        default:
        {
            ERHE_FATAL("Bad mesh mode\n");
        }
    }
}

auto c_str(const Normal_style normal_style) -> const char*
{
    switch (normal_style)
    {
        //using enum Normal_style;
        case Normal_style::none:            return "none";
        case Normal_style::corner_normals:  return "corner_normals";
        case Normal_style::polygon_normals: return "polygon_normals";
        case Normal_style::point_normals:   return "point_normals";
        default:
        {
            ERHE_FATAL("Bad mesh normal style\n");
        }
    }
}

} // namespace erhe::primitive
