#pragma once

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/sphere.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::primitive {

class Buffer_mesh
{
public:
    Buffer_mesh();
    Buffer_mesh(Buffer_mesh&& other);
    Buffer_mesh& operator=(Buffer_mesh&& other);
    Buffer_mesh(const Buffer_mesh&) = delete;
    Buffer_mesh& operator=(const Buffer_mesh&) = delete;

    [[nodiscard]] auto base_vertex(std::size_t stream = 0) const -> uint32_t;
    // base_vertex for the expanded solid-wireframe vertex stream(s).
    [[nodiscard]] auto expanded_base_vertex(std::size_t stream = 0) const -> uint32_t;
    [[nodiscard]] auto base_index () const -> uint32_t;
    [[nodiscard]] auto index_range(Primitive_mode primitive_mode) const -> Index_range;

    erhe::math::Aabb          bounding_box;
    erhe::math::Sphere        bounding_sphere;

    Index_range               triangle_fill_indices   {};
    Index_range               edge_line_indices       {};
    Index_range               corner_point_indices    {};
    Index_range               polygon_centroid_indices{};
    // Sequential index range (values 0..3N-1) into expanded_vertex_buffer_ranges
    // for the solid-wireframe fill draw. Empty when the expanded fill was not built.
    Index_range               expanded_triangle_fill_indices{};

    std::vector<Buffer_range> vertex_buffer_ranges{};
    Buffer_range              index_buffer_range  {};

    // Expanded solid-wireframe fill vertex stream(s): un-shared, 3 sequential
    // vertices per fill triangle, in the expanded vertex format (fill attributes
    // plus custom_attribute_wireframe). Empty when the expanded fill was not
    // built. expanded_vertex_input_key indexes Mesh_memory's vertex-input table
    // for these streams (distinct from vertex_input_key because the format has
    // the extra wireframe attribute).
    std::vector<Buffer_range> expanded_vertex_buffer_ranges{};
    std::size_t               expanded_vertex_input_key{0};

    // Edge line vertex pairs (consecutive pairs of vec4 positions in object-local space)
    Buffer_range              edge_line_vertex_buffer_range{};

    // Optional side buffer holding per-edge-endpoint joint indices + weights
    // (uvec4 + vec4 per vertex), populated only when the source mesh has joint
    // attributes. The Content_wide_line_renderer's skinned compute pipeline
    // reads it alongside edge_line_vertex_buffer_range to apply skinning to
    // edge endpoints. Empty range = mesh has no skinning data.
    Buffer_range              edge_line_joint_buffer_range{};

    size_t                    vertex_input_key{0};

    // RAII allocation handles - freed back to allocator on destruction
    std::vector<erhe::buffer::Buffer_allocation> vertex_allocations{};
    erhe::buffer::Buffer_allocation              index_allocation  {};
    erhe::buffer::Buffer_allocation              edge_line_vertex_allocation{};
    erhe::buffer::Buffer_allocation              edge_line_joint_allocation {};
    std::vector<erhe::buffer::Buffer_allocation> expanded_vertex_allocations{};
};

} // namespace erhe::primitive
