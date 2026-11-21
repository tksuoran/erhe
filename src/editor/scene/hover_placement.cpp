#include "scene/hover_placement.hpp"

#include "config/generated/item_placement_mode.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <algorithm>
#include <limits>

namespace editor {

namespace {

// The least signed distance from the plane to a mesh bounding box corner of the subtree. The world transform of each
// prim is composed here: a subtree outside a scene has had no transform update.
void get_least_distance(
    const erhe::Hierarchy& item,
    const glm::mat4&       world_from_parent,
    const glm::vec3&       plane_point,
    const glm::vec3&       plane_normal,
    float&                 least_distance
)
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&item);
    const glm::mat4 world_from_item = (node != nullptr) ? (world_from_parent * node->parent_from_node()) : world_from_parent;
    const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(&item);
    if (mesh != nullptr) {
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::math::Aabb box = mesh_primitive.primitive->get_bounding_box();
            if (!box.is_valid_3d()) {
                continue;
            }
            for (unsigned int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local{
                    ((corner & 1u) != 0u) ? box.max.x : box.min.x,
                    ((corner & 2u) != 0u) ? box.max.y : box.min.y,
                    ((corner & 4u) != 0u) ? box.max.z : box.min.z
                };
                const glm::vec3 world = glm::vec3{world_from_item * glm::vec4{local, 1.0f}};
                least_distance = std::min(least_distance, glm::dot(world - plane_point, plane_normal));
            }
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
        if (child) {
            get_least_distance(*child, world_from_item, plane_point, plane_normal, least_distance);
        }
    }
}

}

void place_nodes_at(const std::span<erhe::scene::Node* const> nodes, const Hover_placement& placement, const Item_placement_mode mode)
{
    if (nodes.empty()) {
        return;
    }
    glm::vec3 offset = placement.position - glm::vec3{nodes.front()->position_in_world()};
    if ((mode == Item_placement_mode::avoid_intersection) && placement.normal.has_value()) {
        const glm::mat4 move           = erhe::math::create_translation<float>(offset);
        float           least_distance = std::numeric_limits<float>::max();
        for (const erhe::scene::Node* const node : nodes) {
            // world_from_node() of an uninserted root is its transform; the root's own parent_from_node is folded in below.
            const glm::mat4 world_from_parent = move * node->world_from_node() * glm::inverse(node->parent_from_node());
            get_least_distance(*node, world_from_parent, placement.position, placement.normal.value(), least_distance);
        }
        if (least_distance < 0.0f) {
            offset -= least_distance * placement.normal.value();
        }
    }
    const glm::mat4 move = erhe::math::create_translation<float>(offset);
    for (erhe::scene::Node* const node : nodes) {
        node->set_world_from_node(move * node->world_from_node());
    }
}

}
