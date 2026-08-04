#include "erhe_primitive/buffer_mesh.hpp"

namespace erhe::primitive {

auto buffer_mesh_allocation_mutex() -> std::mutex&
{
    static std::mutex mutex;
    return mutex;
}

namespace {

// Frees this mesh's pool allocations. Caller holds
// buffer_mesh_allocation_mutex() - see the comment on it in the header.
void release_allocations(Buffer_mesh& buffer_mesh)
{
    buffer_mesh.vertex_allocations.clear();
    buffer_mesh.index_allocation            = erhe::buffer::Buffer_allocation{};
    buffer_mesh.edge_line_vertex_allocation = erhe::buffer::Buffer_allocation{};
    buffer_mesh.edge_line_joint_allocation  = erhe::buffer::Buffer_allocation{};
    buffer_mesh.expanded_vertex_allocations.clear();
}

}

Buffer_mesh::Buffer_mesh() = default;

Buffer_mesh::~Buffer_mesh()
{
    // Free under the allocation mutex so the frees cannot interleave into
    // another thread's multi-stream allocation transaction (that would
    // desync the per-stream pools - see buffer_mesh_allocation_mutex()).
    // The member destructors that run after this body find the allocations
    // already empty.
    const std::lock_guard<std::mutex> lock{buffer_mesh_allocation_mutex()};
    release_allocations(*this);
}

Buffer_mesh::Buffer_mesh(Buffer_mesh&& other) = default;

Buffer_mesh& Buffer_mesh::operator=(Buffer_mesh&& other)
{
    if (this != &other) {
        // Move-assign frees the destination's old allocations; same
        // no-interleave requirement as the destructor.
        const std::lock_guard<std::mutex> lock{buffer_mesh_allocation_mutex()};
        release_allocations(*this);
        bounding_box                   = other.bounding_box;
        bounding_sphere                = other.bounding_sphere;
        joint_bounding_boxes           = std::move(other.joint_bounding_boxes);
        triangle_fill_indices          = other.triangle_fill_indices;
        edge_line_indices              = other.edge_line_indices;
        corner_point_indices           = other.corner_point_indices;
        polygon_centroid_indices       = other.polygon_centroid_indices;
        expanded_triangle_fill_indices = other.expanded_triangle_fill_indices;
        vertex_buffer_ranges           = std::move(other.vertex_buffer_ranges);
        index_buffer_range             = other.index_buffer_range;
        expanded_vertex_buffer_ranges  = std::move(other.expanded_vertex_buffer_ranges);
        expanded_vertex_input_key      = other.expanded_vertex_input_key;
        edge_line_vertex_buffer_range  = other.edge_line_vertex_buffer_range;
        edge_line_joint_buffer_range   = other.edge_line_joint_buffer_range;
        vertex_input_key               = other.vertex_input_key;
        vertex_allocations             = std::move(other.vertex_allocations);
        index_allocation               = std::move(other.index_allocation);
        edge_line_vertex_allocation    = std::move(other.edge_line_vertex_allocation);
        edge_line_joint_allocation     = std::move(other.edge_line_joint_allocation);
        expanded_vertex_allocations    = std::move(other.expanded_vertex_allocations);
    }
    return *this;
}

auto Buffer_mesh::base_vertex(std::size_t stream) const -> uint32_t
{
    return static_cast<uint32_t>(vertex_buffer_ranges[stream].byte_offset / vertex_buffer_ranges[stream].element_size);
}

auto Buffer_mesh::expanded_base_vertex(std::size_t stream) const -> uint32_t
{
    return static_cast<uint32_t>(expanded_vertex_buffer_ranges[stream].byte_offset / expanded_vertex_buffer_ranges[stream].element_size);
}

// Value that should be added in index range first index
auto Buffer_mesh::base_index() const -> uint32_t
{
    return static_cast<uint32_t>(index_buffer_range.byte_offset / index_buffer_range.element_size);
}

auto Buffer_mesh::index_range(const Primitive_mode primitive_mode) const -> Index_range
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set          : return {};
        case Primitive_mode::polygon_fill     : return triangle_fill_indices;
        case Primitive_mode::edge_lines       : return edge_line_indices;
        case Primitive_mode::corner_points    : return corner_point_indices;
        case Primitive_mode::polygon_centroids: return polygon_centroid_indices;
        case Primitive_mode::solid_wireframe  : return expanded_triangle_fill_indices;
        case Primitive_mode::count            : return {};
        default:                                return {};
    }
}

auto primitive_type(const Primitive_mode primitive_mode) -> Primitive_type
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set          : return Primitive_type::none;
        case Primitive_mode::polygon_fill     : return Primitive_type::triangles;
        case Primitive_mode::edge_lines       : return Primitive_type::lines;
        case Primitive_mode::corner_points    : return Primitive_type::points;
        case Primitive_mode::polygon_centroids: return Primitive_type::points;
        case Primitive_mode::solid_wireframe  : return Primitive_type::triangles;
        case Primitive_mode::count            : return Primitive_type::none;
        default:                                return Primitive_type::none;
    }
}

} // namespace erhe::primitive
