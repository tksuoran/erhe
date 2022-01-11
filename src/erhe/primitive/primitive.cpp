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
        using enum Primitive_mode;
        case not_set:           return "not_set";
        case polygon_fill:      return "polygon_fill";
        case edge_lines:        return "edge_lines";
        case corner_points:     return "corner_points";
        case corner_normals:    return "corner_normals";
        case polygon_centroids: return "polygon_centroids";
        case count:             return "count";
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
        using enum Normal_style;
        case none:            return "none";
        case corner_normals:  return "corner_normals";
        case polygon_normals: return "polygon_normals";
        case point_normals:   return "point_normals";
        default:
        {
            ERHE_FATAL("Bad mesh normal style\n");
        }
    }
}

} // namespace erhe::primitive
