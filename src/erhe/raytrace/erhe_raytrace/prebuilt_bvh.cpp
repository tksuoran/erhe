#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_hash/hash.hpp"

#include <fmt/format.h>

namespace erhe::raytrace {

auto c_str(const Bvh_build_quality quality) -> const char*
{
    switch (quality) {
        case Bvh_build_quality::low:    return "low";
        case Bvh_build_quality::medium: return "medium";
        case Bvh_build_quality::high:   return "high";
        default:                        return "?";
    }
}

auto c_str(const Bvh_source source) -> const char*
{
    switch (source) {
        case Bvh_source::none:     return "none";
        case Bvh_source::built:    return "built";
        case Bvh_source::cache:    return "cache";
        case Bvh_source::prebuilt: return "prebuilt";
        default:                   return "?";
    }
}

auto hash_triangle_positions(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const uint64_t seed) -> uint64_t
{
    uint64_t hash_code = seed;
    hash_code = erhe::hash::hash(p0.x, p0.y, p0.z, hash_code);
    hash_code = erhe::hash::hash(p1.x, p1.y, p1.z, hash_code);
    hash_code = erhe::hash::hash(p2.x, p2.y, p2.z, hash_code);
    return hash_code;
}

auto compute_triangle_hash(const std::span<const uint32_t> indices, const std::span<const float> positions_xyz) -> uint64_t
{
    uint64_t hash_code = erhe::hash::c_seed;
    const auto position = [&](const uint32_t vertex) -> glm::vec3 {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3;
        return glm::vec3{positions_xyz[base + 0], positions_xyz[base + 1], positions_xyz[base + 2]};
    };
    for (std::size_t triangle = 0, end = indices.size() / 3; triangle < end; ++triangle) {
        hash_code = hash_triangle_positions(
            position(indices[(3 * triangle) + 0]),
            position(indices[(3 * triangle) + 1]),
            position(indices[(3 * triangle) + 2]),
            hash_code
        );
    }
    return hash_code;
}

auto get_prebuilt_bvh_backend() -> std::string_view
{
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
    return "bvh_v2";
#else
    return {};
#endif
}

auto get_prebuilt_bvh_library() -> std::string_view
{
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
    return ERHE_BVH_LIBRARY_TAG;
#else
    return {};
#endif
}

namespace {

constexpr uint32_t c_prim_count_bits = 4;
constexpr uint32_t c_prim_count_mask = (1u << c_prim_count_bits) - 1u;
constexpr uint64_t c_max_first_id    = (uint64_t{1} << (32u - c_prim_count_bits)) - 1u;

}

auto validate_prebuilt_bvh(
    const Prebuilt_bvh&    prebuilt,
    const std::string_view expected_backend,
    const std::string_view expected_library,
    const uint64_t         triangle_hash,
    const std::size_t      triangle_count,
    const std::size_t      max_depth
) -> std::string
{
    if (expected_backend.empty()) {
        return "the raytrace backend cannot use a prebuilt BVH";
    }
    if (prebuilt.backend != expected_backend) {
        return fmt::format("backend '{}' does not match '{}'", prebuilt.backend, expected_backend);
    }
    if (prebuilt.library != expected_library) {
        return fmt::format("library '{}' does not match '{}'", prebuilt.library, expected_library);
    }
    if (prebuilt.triangle_count != triangle_count) {
        return fmt::format("triangle count {} does not match {}", prebuilt.triangle_count, triangle_count);
    }
    if (prebuilt.triangle_hash != triangle_hash) {
        return fmt::format("triangle hash {:016x} does not match {:016x}", prebuilt.triangle_hash, triangle_hash);
    }
    const std::size_t node_count = prebuilt.nodes.size();
    if (node_count == 0) {
        return "no nodes";
    }
    if ((node_count > c_max_first_id) || (triangle_count > c_max_first_id)) {
        return fmt::format("{} nodes / {} triangles exceed the 28-bit first_id", node_count, triangle_count);
    }
    if (prebuilt.primitive_ids.size() != triangle_count) {
        return fmt::format("{} primitive ids for {} triangles", prebuilt.primitive_ids.size(), triangle_count);
    }

    // Ranges, and the parent count of each node (node 0 must have none,
    // every other node exactly one inner parent).
    std::vector<uint8_t> parent_count(node_count, 0);
    for (std::size_t n = 0; n < node_count; ++n) {
        const uint32_t value      = prebuilt.nodes[n].index;
        const uint64_t first_id   = value >> c_prim_count_bits;
        const uint32_t prim_count = value & c_prim_count_mask;
        if (prim_count == 0) {
            if ((first_id + 1) >= node_count) {
                return fmt::format("inner node {} references children {} and {} of {} nodes", n, first_id, first_id + 1, node_count);
            }
            for (uint64_t child = first_id; child <= (first_id + 1); ++child) {
                if (child == 0) {
                    return fmt::format("inner node {} references the root", n);
                }
                if (parent_count[child] != 0) {
                    return fmt::format("node {} is a child of more than one inner node", child);
                }
                parent_count[child] = 1;
            }
        } else if ((first_id + prim_count) > triangle_count) {
            return fmt::format("leaf {} references slots {} to {} of {}", n, first_id, first_id + prim_count - 1, triangle_count);
        }
    }
    for (std::size_t n = 1; n < node_count; ++n) {
        if (parent_count[n] == 0) {
            return fmt::format("node {} is not a child of any inner node", n);
        }
    }

    // With one parent per non-root node and none for the root, the nodes
    // form a tree only when all of them are reachable from the root: a
    // cycle detached from the root would also give every node one parent.
    // Walk from the root, which also bounds the depth (count of inner
    // ancestors) by the traversal stack size.
    {
        std::vector<std::pair<uint32_t, uint32_t>> stack; // node, inner ancestor count
        stack.reserve(max_depth + 2);
        stack.emplace_back(0u, 0u);
        std::size_t visited = 0;
        while (!stack.empty()) {
            const std::pair<uint32_t, uint32_t> top = stack.back();
            stack.pop_back();
            ++visited;
            const uint32_t value = prebuilt.nodes[top.first].index;
            if ((value & c_prim_count_mask) != 0) {
                continue;
            }
            const uint32_t child_depth = top.second + 1;
            if (child_depth > max_depth) {
                return fmt::format("depth exceeds {}", max_depth);
            }
            const uint32_t first_id = value >> c_prim_count_bits;
            stack.emplace_back(first_id,     child_depth);
            stack.emplace_back(first_id + 1, child_depth);
        }
        if (visited != node_count) {
            return fmt::format("{} of {} nodes are reachable from the root", visited, node_count);
        }
    }

    std::vector<uint8_t> seen(triangle_count, 0);
    for (const uint32_t id : prebuilt.primitive_ids) {
        if (id >= triangle_count) {
            return fmt::format("primitive id {} out of range {}", id, triangle_count);
        }
        if (seen[id] != 0) {
            return fmt::format("primitive id {} appears more than once", id);
        }
        seen[id] = 1;
    }
    return {};
}

} // namespace erhe::raytrace
