#pragma once

#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_math/math_util.hpp"

#include <vector>

namespace erhe::graphics {
    class Buffer;
}
namespace erhe::raytrace {
    class Buffer;
}
namespace erhe::geometry {
    class Geometry;
}

namespace erhe::primitive
{

class Geometry_mesh
{
public:
    [[nodiscard]] auto base_vertex() const -> uint32_t;
    [[nodiscard]] auto base_index () const -> uint32_t;
    [[nodiscard]] auto index_range(const Primitive_mode primitive_mode) const -> Index_range;

    erhe::math::Bounding_box    bounding_box;
    erhe::math::Bounding_sphere bounding_sphere;

    Index_range  triangle_fill_indices   {};
    Index_range  edge_line_indices       {};
    Index_range  corner_point_indices    {};
    Index_range  polygon_centroid_indices{};

    Buffer_range vertex_buffer_range     {};
    Buffer_range index_buffer_range      {};

    // TODO These make Geometry_mesh expensive to copy
    std::vector<uint32_t> primitive_id_to_polygon_id;
    std::vector<uint32_t> corner_to_vertex_id;
};

} // namespace erhe::primitive
