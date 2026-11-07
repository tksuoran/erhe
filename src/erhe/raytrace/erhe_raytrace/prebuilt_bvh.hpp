#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::raytrace {

// One BVH node in the ERHE_raytrace_bvh "bvh_v2" record layout
// (doc/erhe/gltf.md "Raytrace BVH items"): the same 28 bytes as
// bvh::v2::Node<float, 3>. bounds are min x, max x, min y, max y, min z,
// max z. index packs prim_count in bits 0-3 and first_id in bits 4-31.
class Bvh_node_record
{
public:
    float    bounds[6];
    uint32_t index;
};
static_assert(sizeof(Bvh_node_record) == 28);

// One BVH node in the "tinybvh" record layout (doc/erhe/raytrace.md
// "Prebuilt BVH"): the same 32 bytes as tinybvh::BVH::BVHNode. An inner node
// (tri_count 0) has its children at left_first and left_first + 1; a leaf
// covers primitive slots left_first .. left_first + tri_count - 1.
class Tinybvh_node_record
{
public:
    float    aabb_min[3];
    uint32_t left_first;
    float    aabb_max[3];
    uint32_t tri_count;
};
static_assert(sizeof(Tinybvh_node_record) == 32);

enum class Bvh_build_quality : unsigned int {
    low    = 0,
    medium = 1,
    high   = 2
};

[[nodiscard]] auto c_str(Bvh_build_quality quality) -> const char*;

// A BVH over a geometry's triangles in index order, plus the values that
// tie it to that content and to the library version that built it.
// IGeometry::export_bvh() fills one after commit(); IGeometry::set_prebuilt_bvh()
// takes one before commit().
class Prebuilt_bvh
{
public:
    std::string                      backend;        // "bvh_v2" or "tinybvh"
    std::string                      library;        // pinned GIT_TAG of the backend library
    Bvh_build_quality                quality       {Bvh_build_quality::high};
    uint64_t                         triangle_hash {0};
    std::size_t                      triangle_count{0};
    std::vector<Bvh_node_record>     nodes;          // backend bvh_v2; node 0 is the root
    std::vector<Tinybvh_node_record> tinybvh_nodes;  // backend tinybvh; node 0 is the root
    std::vector<uint32_t>            primitive_ids;  // slot i holds core triangle index
};

// The node record size of a backend's layout: 28 (bvh_v2), 32 (tinybvh), 0 (unknown).
[[nodiscard]] auto get_prebuilt_bvh_node_record_size(std::string_view backend) -> std::size_t;
// The node records of prebuilt.backend's layout, as bytes.
[[nodiscard]] auto get_prebuilt_bvh_node_bytes(const Prebuilt_bvh& prebuilt) -> std::span<const std::byte>;
// Fills the node records of prebuilt.backend's layout from bytes; false for an
// unknown backend or a byte count that is not a whole number of records.
[[nodiscard]] auto set_prebuilt_bvh_node_bytes(Prebuilt_bvh& prebuilt, std::span<const std::byte> bytes) -> bool;

// How the BVH of a committed geometry was obtained.
enum class Bvh_source : unsigned int {
    none     = 0, // not committed, commit failed, or the backend does not report it
    built    = 1,
    prebuilt = 2  // supplied with IGeometry::set_prebuilt_bvh()
};

[[nodiscard]] auto c_str(Bvh_source source) -> const char*;

// Backend name and library tag of the running raytrace backend for
// Prebuilt_bvh. Both are empty when the backend cannot export or import a
// BVH (embree, none).
[[nodiscard]] auto get_prebuilt_bvh_backend() -> std::string_view;
[[nodiscard]] auto get_prebuilt_bvh_library() -> std::string_view;

// The triangle_hash of doc/erhe/gltf.md "Raytrace BVH items": 64-bit FNV-1a (erhe::hash::hash()) over, triangle by
// triangle, the three corner positions in index order, each as three
// float32 values with the bits as stored. hash_triangle_positions() is one
// triangle's step, seeded with the hash so far (erhe::hash::c_seed for the
// first triangle); Bvh_geometry::commit() hashes with it.
[[nodiscard]] auto hash_triangle_positions(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, uint64_t seed) -> uint64_t;

// The same hash over an indexed triangle list: triangle t is
// indices[3t .. 3t + 2], positions_xyz holds three floats per vertex. The
// caller guarantees every index is below positions_xyz.size() / 3.
[[nodiscard]] auto compute_triangle_hash(std::span<const uint32_t> indices, std::span<const float> positions_xyz) -> uint64_t;

// The traversal stack size of the bvh_v2 backend: the deepest a prebuilt
// bvh_v2 BVH may be (validate_prebuilt_bvh() max_depth).
constexpr std::size_t bvh_v2_traversal_stack_size = 64;
// The same for tinybvh (TINYBVH_STACK_SIZE, the node stack of tinybvh::BVH::Intersect()).
constexpr std::size_t tinybvh_traversal_stack_size = 128;
// The traversal stack size of the running backend (0 without one).
[[nodiscard]] auto get_prebuilt_bvh_max_depth() -> std::size_t;

// Checks a Prebuilt_bvh against the content it claims to describe, per
// doc/erhe/raytrace.md "Prebuilt BVH": backend and library tag, triangle
// count and hash, the node records of the backend's layout (child and
// primitive ranges, a tree from node 0 of depth at most max_depth: bvh_v2
// every node in it, tinybvh unused nodes allowed) and that primitive_ids is
// a permutation. Returns an empty string when the BVH is acceptable,
// otherwise the reason it is rejected.
[[nodiscard]] auto validate_prebuilt_bvh(
    const Prebuilt_bvh& prebuilt,
    std::string_view    expected_backend,
    std::string_view    expected_library,
    uint64_t            triangle_hash,
    std::size_t         triangle_count,
    std::size_t         max_depth
) -> std::string;

} // namespace erhe::raytrace
