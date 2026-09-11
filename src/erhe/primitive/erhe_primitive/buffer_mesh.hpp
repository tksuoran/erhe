#pragma once

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/sphere.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace erhe::primitive {

// Bounds of one texcoord channel over the vertices of a Buffer_mesh. Only
// channels 0 and 1 are tracked: channel 2 is the lightmap UV set, which is in
// [0, 1] by construction and is stored without an affine.
class Texcoord_range
{
public:
    glm::vec2 min  {0.0f, 0.0f};
    glm::vec2 max  {1.0f, 1.0f};
    bool      valid{false};

    void add(const glm::vec2 uv)
    {
        if (!valid) {
            min   = uv;
            max   = uv;
            valid = true;
            return;
        }
        min = glm::min(min, uv);
        max = glm::max(max, uv);
    }
};

static constexpr std::size_t affine_texcoord_channel_count = 2;

// Dequantization affine for the two affine texcoord channels of one primitive:
// xy is channel 0, zw is channel 1, and decoded = a_texcoord * scale + offset.
// Encoder and decoder must agree exactly, so both go through
// get_texcoord_quantization() below - unlike the position affine, which is
// derived independently at three sites.
class Texcoord_quantization
{
public:
    glm::vec4 scale {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 offset{0.0f, 0.0f, 0.0f, 0.0f};
};

// Serializes GPU vertex/index pool traffic of buffer meshes. The vertex-pool
// lockstep invariant (one indirect-draw vertexOffset applied to every stream
// binding, see buffer_pool.hpp) requires the per-stream pools of a vertex
// format to see IDENTICAL allocation/free histories - only then do the
// element cursors and hole patterns stay isomorphic across pools and every
// mesh's streams land at matching element offsets. Concurrent builders
// (deferred glTF finalize tasks, async mesh operations) must therefore make
// each multi-stream allocation group one atomic transaction, and frees (a
// Buffer_mesh being destroyed or move-assigned over) must not interleave
// into a transaction either - both take this mutex. Innermost lock: nothing
// else is acquired while it is held.
[[nodiscard]] auto buffer_mesh_allocation_mutex() -> std::mutex&;

class Buffer_mesh
{
public:
    Buffer_mesh();
    ~Buffer_mesh();
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

    // UV bounds of texcoord channels 0 and 1 over this mesh's vertices, from
    // which get_texcoord_quantization() derives the affine. Populated by every
    // build; only READ when the vertex format stores those channels as
    // unorm16x2, so it is inert for the float base variant.
    std::array<Texcoord_range, affine_texcoord_channel_count> texcoord_ranges{};

    // Per-joint rest-pose bounds, indexed by joint index (the same index space
    // as the JOINTS_n vertex attribute and erhe::scene::Skin_data::joints).
    // Entry i bounds every vertex that joint i influences with a non-zero
    // weight; entries for joints that influence nothing stay invalid.
    //
    // Used to bound a GPU-skinned mesh in world space: a skinned position is
    // sum(w_i * world_from_bind_i * p) with sum(w_i) == 1, i.e. a convex
    // combination of the per-joint transformed points, so it lies inside the
    // union of the per-joint boxes transformed by their world_from_bind. The
    // whole-mesh bounding_box above is the REST pose and (per glTF) is not
    // transformed by the mesh node, so it cannot bound a skinned mesh.
    //
    // Empty when the source geometry has no joint attributes.
    std::vector<erhe::math::Aabb> joint_bounding_boxes;

    Index_range               triangle_fill_indices   {};
    Index_range               edge_line_indices       {};
    Index_range               corner_point_indices    {};
    Index_range               polygon_centroid_indices{};
    // Sequential index range (values 0..3N-1) into expanded_vertex_buffer_ranges
    // for the solid-wireframe fill draw. Empty when the expanded fill was not built.
    Index_range               expanded_triangle_fill_indices{};

    std::vector<Buffer_range> vertex_buffer_ranges{}; // per stream
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

    // Whether the source authored vertex colors (a Geometry with a
    // corner/vertex color attribute, a soup whose format carries color 0).
    // The mesh memory vertex format always carries a color attribute, filled
    // with white when the source has none, so the format cannot answer this;
    // the material slot writer needs it to tell an unbound primitive whose
    // colors are its albedo from one that renders the default look
    // (Material_set::vertex_colored_default_material_slot_index).
    bool                      has_vertex_colors{false};

    // RAII allocation handles - freed back to allocator on destruction
    std::vector<erhe::buffer::Buffer_allocation> vertex_allocations{};
    erhe::buffer::Buffer_allocation              index_allocation  {};
    erhe::buffer::Buffer_allocation              edge_line_vertex_allocation{};
    erhe::buffer::Buffer_allocation              edge_line_joint_allocation {};
    std::vector<erhe::buffer::Buffer_allocation> expanded_vertex_allocations{};
};

// The one derivation of the texcoord dequantization affine, shared by both
// encode paths and by the per-primitive record writers:
//   scale  = max(max - min, epsilon)  per axis
//   offset = min
// A channel with no recorded range - and a degenerate axis - yields the
// identity, so an unencoded channel decodes to itself.
[[nodiscard]] auto get_texcoord_quantization(const Buffer_mesh& buffer_mesh) -> Texcoord_quantization;

} // namespace erhe::primitive
