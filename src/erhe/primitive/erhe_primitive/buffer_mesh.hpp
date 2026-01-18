#pragma once

#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/sphere.hpp"

#include <cstddef>
#include <cstdint>

namespace erhe::primitive {

class Buffer_mesh
{
public:
    [[nodiscard]] auto base_vertex(std::size_t stream = 0) const -> uint32_t;
    [[nodiscard]] auto base_index () const -> uint32_t;
    [[nodiscard]] auto index_range(Primitive_mode primitive_mode) const -> Index_range;

    erhe::math::Aabb   bounding_box;
    erhe::math::Sphere bounding_sphere;

    Index_range triangle_fill_indices   {};
    Index_range edge_line_indices       {};
    Index_range corner_point_indices    {};
    Index_range polygon_centroid_indices{};

    std::vector<Buffer_range> vertex_buffer_ranges{};
    Buffer_range              index_buffer_range  {};
};

} // namespace erhe::primitive
