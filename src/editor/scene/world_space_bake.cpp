#include "scene/world_space_bake.hpp"

#include "content_library/content_library.hpp"
#include "scene/generated/scene_export_settings.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item_flags.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <glm/glm.hpp>

#include <unordered_map>

namespace editor {

namespace {

// Estimated bytes of one baked copy of a primitive: the core's vertices
// (position, normal, tangent, one texcoord set) and its uint32 indices.
constexpr std::size_t c_estimated_core_vertex_bytes = 64;

class Planner
{
public:
    Planner(
        const Scene_root&                                   scene_root,
        const Scene_export_settings&                        settings,
        const std::unordered_set<const erhe::scene::Node*>& external_asset_nodes,
        World_space_bake_plan&                              plan
    )
        : m_settings            {settings}
        , m_external_asset_nodes{external_asset_nodes}
        , m_plan                {plan}
    {
        const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
        if (content_library) {
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
            for (const std::shared_ptr<erhe::scene::Animation>& animation : content_library->get_all<erhe::scene::Animation>()) {
                for (const erhe::scene::Animation_channel& channel : animation->channels) {
                    const std::shared_ptr<erhe::scene::Xformable> target = erhe::scene::get_target_node(channel);
                    if (target) {
                        m_animated_nodes.insert(target.get());
                    }
                }
            }
        }
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            if (mesh->skin) {
                for (const std::shared_ptr<erhe::scene::Node>& joint : mesh->skin->skin_data.joints) {
                    if (joint) {
                        m_joint_nodes.insert(joint.get());
                    }
                }
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape) {
                    ++m_shape_users[mesh_primitive.primitive->render_shape.get()];
                }
            }
        }
        m_budget_bytes = static_cast<std::size_t>(std::max(settings.instancing_budget_mib, 0)) * 1024u * 1024u;
    }

    void run(const Scene_root& scene_root)
    {
        const std::shared_ptr<erhe::scene::Node> root = scene_root.get_scene().get_root_node();
        if (!root) {
            return;
        }
        const glm::mat4 root_from_world = glm::inverse(root->world_from_node());
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            std::size_t duplicated_bytes = 0;
            if (!can_bake(*mesh, duplicated_bytes)) {
                continue;
            }
            m_used_bytes += duplicated_bytes;
            m_plan.bake.baked_meshes.emplace(mesh.get(), root_from_world * mesh->world_from_node());
        }
        m_plan.duplicated_bytes = m_used_bytes;
    }

private:
    [[nodiscard]] auto keep(const char* reason) -> bool
    {
        ++m_plan.kept_reasons[reason];
        return false;
    }

    // The node and its ancestors: none moves at runtime or is written as an
    // external asset reference.
    [[nodiscard]] auto check_ancestry(const erhe::scene::Node& node) -> bool
    {
        std::shared_ptr<erhe::scene::Node> holder;
        for (const erhe::scene::Node* current = &node; current != nullptr; current = holder.get()) {
            if (m_animated_nodes.contains(current)) {
                return keep("animated (or under an animated node)");
            }
            if (m_external_asset_nodes.contains(current)) {
                return keep("external asset");
            }
            const std::optional<Node_physics_data> physics = read_node_physics(*current);
            if (
                physics.has_value() &&
                (physics->motion_mode != erhe::physics::Motion_mode::e_none) &&
                (physics->motion_mode != erhe::physics::Motion_mode::e_static)
            ) {
                return keep("moved by a rigid body");
            }
            holder = current->get_parent_node();
        }
        return true;
    }

    [[nodiscard]] auto can_bake(const erhe::scene::Mesh& mesh, std::size_t& out_duplicated_bytes) -> bool
    {
        out_duplicated_bytes = 0;
        if ((mesh.get_flag_bits() & erhe::Item_flags::content) == 0) {
            return keep("not content");
        }
        if (mesh.skin) {
            return keep("skinned");
        }
        if (m_joint_nodes.contains(&mesh)) {
            return keep("skin joint");
        }
        if (read_node_physics(mesh).has_value()) {
            return keep("physics"); // collider shapes live in the node's space
        }
        if (!check_ancestry(mesh)) {
            return false;
        }
        ++m_plan.candidate_mesh_count;
        std::size_t shared_bytes = 0;
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
            if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape) {
                continue;
            }
            const erhe::primitive::Primitive_render_shape& shape = *mesh_primitive.primitive->render_shape;
            if (!shape.has_triangle_soup()) {
                return keep("authored geometry primitive"); // written as ERHE_geometry, not prepared
            }
            const auto users = m_shape_users.find(&shape);
            if ((users != m_shape_users.end()) && (users->second > 1)) {
                const erhe::primitive::Triangle_soup_counts counts = shape.get_triangle_soup_counts();
                shared_bytes += (counts.vertex_count * c_estimated_core_vertex_bytes) + (counts.index_count * sizeof(uint32_t));
            }
        }
        if (shared_bytes > 0) {
            switch (m_settings.instancing) {
                case Bake_instancing::skip: {
                    ++m_plan.shared_skip_count;
                    return keep("shared primitive (instancing skip)");
                }
                case Bake_instancing::duplicate_within_budget: {
                    if ((m_used_bytes + shared_bytes) > m_budget_bytes) {
                        ++m_plan.shared_skip_count;
                        return keep("shared primitive (over instancing budget)");
                    }
                    break;
                }
                case Bake_instancing::duplicate:
                default: {
                    break;
                }
            }
        }
        out_duplicated_bytes = shared_bytes;
        return true;
    }

    const Scene_export_settings&                                     m_settings;
    const std::unordered_set<const erhe::scene::Node*>&              m_external_asset_nodes;
    World_space_bake_plan&                                           m_plan;
    std::unordered_set<const erhe::scene::Node*>                     m_animated_nodes;
    std::unordered_set<const erhe::scene::Node*>                     m_joint_nodes;
    std::unordered_map<const erhe::primitive::Primitive_render_shape*, std::size_t> m_shape_users;
    std::size_t                                                      m_budget_bytes{0};
    std::size_t                                                      m_used_bytes{0};
};

} // anonymous namespace

auto plan_world_space_bake(
    const Scene_root&                                   scene_root,
    const Scene_export_settings&                        settings,
    const std::unordered_set<const erhe::scene::Node*>& external_asset_nodes
) -> World_space_bake_plan
{
    ERHE_PROFILE_FUNCTION();

    World_space_bake_plan plan{};
    Planner planner{scene_root, settings, external_asset_nodes, plan};
    planner.run(scene_root);
    return plan;
}

} // namespace editor
