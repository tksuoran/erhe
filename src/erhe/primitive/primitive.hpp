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

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{

struct Index_range;
struct Material;

struct Primitive_geometry final
{
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

    void allocate_vertex_buffer(size_t                  vertex_count,
                                size_t                  vertex_element_size,
                                gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit);

    void allocate_vertex_buffer(const std::shared_ptr<erhe::graphics::Buffer>& buffer,
                                size_t                                         vertex_count,
                                size_t                                         vertex_element_size);

    void allocate_index_buffer(size_t                  index_count,
                               size_t                  index_element_size,
                               gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit);

    void allocate_index_buffer(const std::shared_ptr<erhe::graphics::Buffer>& buffer,
                               size_t                                         index_count,
                               size_t                                         index_element_size);

    auto index_range(Primitive_mode primitive_mode) const -> Index_range;

    glm::vec3                               bounding_box_min{std::numeric_limits<float>::max()}; // bounding box
    glm::vec3                               bounding_box_max{std::numeric_limits<float>::lowest()};
    Index_range                             fill_indices;
    Index_range                             edge_line_indices;
    Index_range                             corner_point_indices;
    Index_range                             polygon_centroid_indices;

    std::shared_ptr<erhe::graphics::Buffer> vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer> index_buffer;
    size_t                                  vertex_byte_offset {0};
    size_t                                  index_byte_offset  {0};
    size_t                                  vertex_element_size{0};
    size_t                                  index_element_size {0};
    size_t                                  vertex_count       {0};
    size_t                                  index_count        {0};

    std::shared_ptr<erhe::geometry::Geometry> source_geometry;
    Normal_style                              source_normal_style{Normal_style::corner_normals};
};

struct Primitive
{
    Primitive();
    Primitive(std::shared_ptr<Primitive_geometry> primitive_geometry,
              std::shared_ptr<Material>           material);

    std::shared_ptr<Primitive_geometry> primitive_geometry;
    std::shared_ptr<Material>           material;
};

std::optional<gl::Primitive_type> primitive_type(Primitive_mode primitive_mode);

} // namespace erhe::primitive
