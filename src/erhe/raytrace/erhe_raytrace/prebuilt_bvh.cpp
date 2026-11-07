#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_hash/hash.hpp"

#include <fmt/format.h>

#include <cstring>
#include <limits>

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
#elif defined(ERHE_RAYTRACE_LIBRARY_TINYBVH)
    return "tinybvh";
#else
    return {};
#endif
}

auto get_prebuilt_bvh_library() -> std::string_view
{
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
    return ERHE_BVH_LIBRARY_TAG;
#elif defined(ERHE_RAYTRACE_LIBRARY_TINYBVH)
    return ERHE_TINYBVH_LIBRARY_TAG;
#else
    return {};
#endif
}

auto get_prebuilt_bvh_max_depth() -> std::size_t
{
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
    return bvh_v2_traversal_stack_size;
#elif defined(ERHE_RAYTRACE_LIBRARY_TINYBVH)
    return tinybvh_traversal_stack_size;
#else
    return 0;
#endif
}

auto get_prebuilt_bvh_node_record_size(const std::string_view backend) -> std::size_t
{
    if (backend == "bvh_v2") {
        return sizeof(Bvh_node_record);
    }
    if (backend == "tinybvh") {
        return sizeof(Tinybvh_node_record);
    }
    return 0;
}

auto get_prebuilt_bvh_node_bytes(const Prebuilt_bvh& prebuilt) -> std::span<const std::byte>
{
    if (prebuilt.backend == "tinybvh") {
        return std::as_bytes(std::span<const Tinybvh_node_record>{prebuilt.tinybvh_nodes});
    }
    return std::as_bytes(std::span<const Bvh_node_record>{prebuilt.nodes});
}

auto set_prebuilt_bvh_node_bytes(Prebuilt_bvh& prebuilt, const std::span<const std::byte> bytes) -> bool
{
    const std::size_t record_size = get_prebuilt_bvh_node_record_size(prebuilt.backend);
    if ((record_size == 0) || ((bytes.size() % record_size) != 0)) {
        return false;
    }
    const std::size_t count = bytes.size() / record_size;
    if (prebuilt.backend == "tinybvh") {
        prebuilt.tinybvh_nodes.resize(count);
        std::memcpy(prebuilt.tinybvh_nodes.data(), bytes.data(), bytes.size());
    } else {
        prebuilt.nodes.resize(count);
        std::memcpy(prebuilt.nodes.data(), bytes.data(), bytes.size());
    }
    return true;
}

namespace {

constexpr uint32_t c_prim_count_bits = 4;
constexpr uint32_t c_prim_count_mask = (1u << c_prim_count_bits) - 1u;
constexpr uint64_t c_max_first_id    = (uint64_t{1} << (32u - c_prim_count_bits)) - 1u;

[[nodiscard]] auto validate_bvh_v2_nodes(const Prebuilt_bvh& prebuilt, const std::size_t triangle_count, const std::size_t max_depth) -> std::string
{
    const std::size_t node_count = prebuilt.nodes.size();
    if (node_count == 0) {
        return "no nodes";
    }
    if ((node_count > c_max_first_id) || (triangle_count > c_max_first_id)) {
        return fmt::format("{} nodes / {} triangles exceed the 28-bit first_id", node_count, triangle_count);
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

    return {};
}

// tinybvh: node 0 is the root and node 1 unused (its builder starts at 2);
// nodes no inner node references are unused (threaded builds leave holes).
[[nodiscard]] auto validate_tinybvh_nodes(const Prebuilt_bvh& prebuilt, const std::size_t triangle_count, const std::size_t max_depth) -> std::string
{
    const std::size_t node_count = prebuilt.tinybvh_nodes.size();
    if (node_count == 0) {
        return "no nodes";
    }
    if ((node_count > std::numeric_limits<uint32_t>::max()) || (triangle_count > std::numeric_limits<uint32_t>::max())) {
        return fmt::format("{} nodes / {} triangles exceed 32-bit indices", node_count, triangle_count);
    }
    std::vector<uint8_t> parent_count(node_count, 0);
    std::vector<std::pair<uint32_t, uint32_t>> stack; // node, inner ancestor count
    stack.reserve(max_depth + 2);
    stack.emplace_back(0u, 0u);
    while (!stack.empty()) {
        const std::pair<uint32_t, uint32_t> top = stack.back();
        stack.pop_back();
        const Tinybvh_node_record& node = prebuilt.tinybvh_nodes[top.first];
        if (node.tri_count > 0) {
            if ((static_cast<uint64_t>(node.left_first) + node.tri_count) > triangle_count) {
                return fmt::format("leaf {} references slots {} to {} of {}", top.first, node.left_first, static_cast<uint64_t>(node.left_first) + node.tri_count - 1, triangle_count);
            }
            continue;
        }
        const uint64_t left = node.left_first;
        if ((left + 1) >= node_count) {
            return fmt::format("inner node {} references children {} and {} of {} nodes", top.first, left, left + 1, node_count);
        }
        const uint32_t child_depth = top.second + 1;
        if (child_depth > max_depth) {
            return fmt::format("depth exceeds {}", max_depth);
        }
        for (uint64_t child = left; child <= (left + 1); ++child) {
            if (child == 0) {
                return fmt::format("inner node {} references the root", top.first);
            }
            if (parent_count[child] != 0) {
                return fmt::format("node {} is a child of more than one inner node", child);
            }
            parent_count[child] = 1;
            stack.emplace_back(static_cast<uint32_t>(child), child_depth);
        }
    }
    return {};
}

} // anonymous namespace

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
    if (prebuilt.primitive_ids.size() != triangle_count) {
        return fmt::format("{} primitive ids for {} triangles", prebuilt.primitive_ids.size(), triangle_count);
    }
    const std::string node_reason = (prebuilt.backend == "tinybvh")
        ? validate_tinybvh_nodes(prebuilt, triangle_count, max_depth)
        : validate_bvh_v2_nodes(prebuilt, triangle_count, max_depth);
    if (!node_reason.empty()) {
        return node_reason;
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
