#pragma once

#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_verify/verify.hpp"

#include "tiny_bvh.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace erhe::raytrace {

// Entry distance of the ray into the node's box within [t_near, t_far], or infinity for a miss.
[[nodiscard]] inline auto tinybvh_node_entry(const tinybvh::BVH::BVHNode& node, const Ray& ray, const glm::vec3& inv_direction) -> float
{
    constexpr float miss = std::numeric_limits<float>::infinity();
    const glm::vec3 box_min{node.aabbMin.x, node.aabbMin.y, node.aabbMin.z};
    const glm::vec3 box_max{node.aabbMax.x, node.aabbMax.y, node.aabbMax.z};
    float t_min = ray.t_near;
    float t_max = ray.t_far;
    for (int axis = 0; axis < 3; ++axis) {
        if (ray.direction[axis] == 0.0f) {
            // Parallel to the slab: inside it or a miss (avoids 0 * inf).
            if ((ray.origin[axis] < box_min[axis]) || (ray.origin[axis] > box_max[axis])) {
                return miss;
            }
            continue;
        }
        const float t0 = (box_min[axis] - ray.origin[axis]) * inv_direction[axis];
        const float t1 = (box_max[axis] - ray.origin[axis]) * inv_direction[axis];
        t_min = std::max(t_min, std::min(t0, t1));
        t_max = std::min(t_max, std::max(t0, t1));
    }
    return (t_min <= t_max) ? t_min : miss;
}

// Calls leaf_fn(first, count) for the leaves of a Wald-layout tinybvh::BVH that the ray enters, nearer child first.
// ray.t_far is read at every node, so a leaf that shortens it culls the rest.
template <typename Leaf_fn>
void traverse_tinybvh_nodes(const tinybvh::BVH& bvh, const Ray& ray, Leaf_fn&& leaf_fn)
{
    if ((bvh.usedNodes == 0) || (bvh.bvhNode == nullptr)) {
        return;
    }
    constexpr float miss = std::numeric_limits<float>::infinity();
    const glm::vec3 inv_direction{1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z};
    std::array<uint32_t, tinybvh_traversal_stack_size> stack;
    std::size_t stack_size{0};
    if (!(tinybvh_node_entry(bvh.bvhNode[0], ray, inv_direction) < miss)) {
        return;
    }
    stack[stack_size++] = 0;
    while (stack_size > 0) {
        const tinybvh::BVH::BVHNode& node = bvh.bvhNode[stack[--stack_size]];
        // Tested again: a leaf visited since the push can have shortened the ray.
        if (!(tinybvh_node_entry(node, ray, inv_direction) < miss)) {
            continue;
        }
        if (node.isLeaf()) {
            leaf_fn(node.leftFirst, node.triCount);
            continue;
        }
        uint32_t near_index = node.leftFirst;
        uint32_t far_index  = node.leftFirst + 1;
        float    near_entry = tinybvh_node_entry(bvh.bvhNode[near_index], ray, inv_direction);
        float    far_entry  = tinybvh_node_entry(bvh.bvhNode[far_index],  ray, inv_direction);
        if (far_entry < near_entry) {
            std::swap(near_index, far_index);
            std::swap(near_entry, far_entry);
        }
        // Depth is bounded by TINYBVH_STACK_SIZE, which tinybvh's own traversal assumes as well.
        ERHE_VERIFY((stack_size + 2) <= stack.size());
        if (far_entry < miss) {
            stack[stack_size++] = far_index;
        }
        if (near_entry < miss) {
            stack[stack_size++] = near_index;
        }
    }
}

} // namespace erhe::raytrace
