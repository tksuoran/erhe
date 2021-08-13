#pragma once

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
class Geometry_uploader;

class Primitive_geometry
{
public:
    Primitive_geometry ();
    ~Primitive_geometry();
    Primitive_geometry (Primitive_geometry&) = delete;
    void operator=     (Primitive_geometry&) = delete;
    Primitive_geometry (Primitive_geometry&& other) noexcept;
    auto operator=     (Primitive_geometry&& other) noexcept -> Primitive_geometry&;

    // Specifies a constant that should be added to each element of indices when chosing elements from the enabled vertex arrays.
    auto base_vertex() const -> uint32_t
    {
        return static_cast<uint32_t>(vertex_byte_offset / vertex_element_size);
    }

    // Value that should be added in index range first index
    auto base_index() const
    -> uint32_t
    {
        return static_cast<uint32_t>(index_byte_offset / index_element_size);
    }

    void allocate_vertex_buffer(const Geometry_uploader& uploader,
                                const size_t             vertex_count,
                                const size_t             vertex_element_size);

    void allocate_index_buffer(const Geometry_uploader& uploader,
                               const size_t             index_count,
                               const size_t             index_element_size);

    auto index_range(Primitive_mode primitive_mode) const -> Index_range;

    glm::vec3   bounding_box_min{std::numeric_limits<float>::max()}; // bounding box
    glm::vec3   bounding_box_max{std::numeric_limits<float>::lowest()};
    Index_range triangle_fill_indices;
    Index_range edge_line_indices;
    Index_range corner_point_indices;
    Index_range polygon_centroid_indices;

    size_t      vertex_byte_offset {0};
    size_t      index_byte_offset  {0};
    size_t      vertex_element_size{0};
    size_t      index_element_size {0};
    size_t      vertex_count       {0};
    size_t      index_count        {0};

    std::shared_ptr<erhe::geometry::Geometry> source_geometry;
    Normal_style                              source_normal_style{Normal_style::corner_normals};

    std::shared_ptr<erhe::graphics::Buffer>   gl_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer>   gl_index_buffer;
    std::shared_ptr<erhe::raytrace::Buffer>   embree_vertex_buffer;
    std::shared_ptr<erhe::raytrace::Buffer>   embree_index_buffer;
};

} // namespace erhe::primitive
