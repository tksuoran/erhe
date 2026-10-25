#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::raytrace {

// One BVH node in the ERHE_raytrace_bvh "bvh_v2" record layout
// (doc/gltf_extensions/ERHE_raytrace_bvh.md): the same 28 bytes as
// bvh::v2::Node<float, 3>. bounds are min x, max x, min y, max y, min z,
// max z. index packs prim_count in bits 0-3 and first_id in bits 4-31.
class Bvh_node_record
{
public:
    float    bounds[6];
    uint32_t index;
};
static_assert(sizeof(Bvh_node_record) == 28);

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
    std::string                  backend;        // "bvh_v2"
    std::string                  library;        // pinned GIT_TAG of the backend library
    Bvh_build_quality            quality       {Bvh_build_quality::high};
    uint64_t                     triangle_hash {0};
    std::size_t                  triangle_count{0};
    std::vector<Bvh_node_record> nodes;          // node 0 is the root
    std::vector<uint32_t>        primitive_ids;  // slot i holds core triangle index
};

// How the BVH of a committed geometry was obtained.
enum class Bvh_source : unsigned int {
    none     = 0, // not committed, commit failed, or the backend does not report it
    built    = 1,
    cache    = 2, // loaded from cache/<backend>/<library>/<hash>
    prebuilt = 3  // supplied with IGeometry::set_prebuilt_bvh()
};

[[nodiscard]] auto c_str(Bvh_source source) -> const char*;

// Backend name and library tag of the running raytrace backend for
// Prebuilt_bvh. Both are empty when the backend cannot export or import a
// BVH (embree, tinybvh, none).
[[nodiscard]] auto get_prebuilt_bvh_backend() -> std::string_view;
[[nodiscard]] auto get_prebuilt_bvh_library() -> std::string_view;

// Checks the backend-neutral structure of a bvh_v2 Prebuilt_bvh against the
// content it claims to describe, per doc/gltf_extensions/ERHE_raytrace_bvh.md:
// backend and library tag, triangle count and hash, node count, child and
// primitive ranges, the tree shape (node 0 is a child of no node, every
// other node a child of exactly one inner node, depth at most max_depth)
// and that primitive_ids is a permutation. Returns an empty string when the
// BVH is acceptable, otherwise the reason it is rejected.
[[nodiscard]] auto validate_prebuilt_bvh(
    const Prebuilt_bvh& prebuilt,
    std::string_view    expected_backend,
    std::string_view    expected_library,
    uint64_t            triangle_hash,
    std::size_t         triangle_count,
    std::size_t         max_depth
) -> std::string;

} // namespace erhe::raytrace
