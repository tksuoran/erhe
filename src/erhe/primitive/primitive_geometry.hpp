#pragma once

#include "erhe/primitive/buffer_range.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/enums.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::graphics
{
    class Buffer;
}

namespace erhe::raytrace
{
    class Buffer;
}

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{

class Index_range;

class Primitive_geometry
{
public:
    [[nodiscard]] auto base_vertex() const -> uint32_t;
    [[nodiscard]] auto base_index () const -> uint32_t;
    [[nodiscard]] auto index_range(const Primitive_mode primitive_mode) const -> Index_range;

    glm::vec3    bounding_box_min        {std::numeric_limits<float>::max()}; // bounding box
    glm::vec3    bounding_box_max        {std::numeric_limits<float>::lowest()};
    Index_range  triangle_fill_indices   {};
    Index_range  edge_line_indices       {};
    Index_range  corner_point_indices    {};
    Index_range  polygon_centroid_indices{};

    Buffer_range vertex_buffer_range     {};
    Buffer_range index_buffer_range      {};

    std::vector<uint32_t> primitive_id_to_polygon_id;
};

} // namespace erhe::primitive
