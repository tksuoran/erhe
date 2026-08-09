#include "operations/merge_static_subtree_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <functional>
#include <unordered_map>

namespace editor {

using erhe::geometry::to_geo_mat4f;

Merge_static_subtree_operation::Merge_static_subtree_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    if (!m_parameters.root) {
        return;
    }

    // Breadth-first over rigid segments: each boundary node (nested sway
    // spine / rigid body) found under a target becomes its own target, so
    // one call flattens the whole assembly segment by segment.
    std::vector<std::shared_ptr<erhe::scene::Node>> pending{m_parameters.root};
    while (!pending.empty()) {
        const std::shared_ptr<erhe::scene::Node> root = pending.front();
        pending.erase(pending.begin());
        std::vector<std::shared_ptr<erhe::scene::Node>> boundaries;
        build_target(root, boundaries);
        if (m_parameters.recurse) {
            pending.insert(pending.end(), boundaries.begin(), boundaries.end());
        }
    }

    std::size_t merged_count = 0;
    for (const Target& target : m_targets) {
        merged_count += target.removed_nodes.size();
    }
    set_description(
        fmt::format(
            "[{}] Merge static subtree '{}': {} nodes into {} segments",
            get_serial(),
            m_parameters.root->get_name(),
            merged_count,
            m_targets.size()
        )
    );
}

auto Merge_static_subtree_operation::get_merged_count() const -> std::size_t
{
    std::size_t merged_count = 0;
    for (const Target& target : m_targets) {
        merged_count += target.removed_nodes.size();
    }
    return merged_count;
}

void Merge_static_subtree_operation::build_target(
    const std::shared_ptr<erhe::scene::Node>&        root,
    std::vector<std::shared_ptr<erhe::scene::Node>>& out_boundaries
)
{
    using erhe::scene::Mesh;
    using erhe::scene::Mesh_primitive;
    using erhe::scene::Node;

    Target target;
    target.root = root;
    target.mesh = erhe::scene::get_attachment<Mesh>(root.get());

    // A node is merged only when a Mesh is its sole attachment and every
    // primitive carries source geometry - anything else (joint anchors,
    // group nodes, sensor bodies, buffer-only meshes) is kept.
    const auto is_mergeable = [](Node* node) -> std::shared_ptr<Mesh> {
        const std::vector<std::shared_ptr<erhe::scene::Node_attachment>>& attachments = node->get_attachments();
        if (attachments.size() != 1) {
            return {};
        }
        const std::shared_ptr<Mesh> mesh = std::dynamic_pointer_cast<Mesh>(attachments.front());
        if (!mesh || mesh->get_primitives().empty()) {
            return {};
        }
        for (const Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape || !mesh_primitive.primitive->render_shape->get_geometry()) {
                return {};
            }
        }
        return mesh;
    };

    // Material -> combined geometry, in first-seen order so the result is
    // deterministic across runs.
    std::vector<std::shared_ptr<erhe::primitive::Material>>   material_order;
    std::vector<std::shared_ptr<erhe::geometry::Geometry>>    combined_geometries;
    erhe::primitive::Normal_style                             normal_style = erhe::primitive::Normal_style::none;
    const glm::mat4 root_node_from_world = root->node_from_world();

    enum class Disposition : unsigned int { kept = 0, boundary, merged, pruned };
    std::vector<std::shared_ptr<Node>>       visited_preorder;
    std::unordered_map<Node*, Disposition>   dispositions;
    std::shared_ptr<Mesh>                    first_source_mesh;

    // Post-order classification: a node's disposition depends on its
    // children (pruning), and reparenting depends on the parent's
    // disposition, so classify everything first and derive the removal /
    // reparent lists from the recorded pre-order afterwards.
    const std::function<Disposition(const std::shared_ptr<Node>&)> classify = [&](const std::shared_ptr<Node>& node) -> Disposition {
        visited_preorder.push_back(node);
        const bool boundary =
            node->is_no_transform_update() ||
            (erhe::scene::get_attachment<Node_physics>(node.get()) != nullptr);
        if (boundary) {
            out_boundaries.push_back(node);
            dispositions[node.get()] = Disposition::boundary;
            return Disposition::boundary; // its subtree belongs to its own rigid segment
        }
        bool has_children      = false;
        bool all_children_removed = true;
        for (const auto& child : node->get_children()) {
            if (!erhe::is<Node>(child.get())) {
                all_children_removed = false;
                continue;
            }
            has_children = true;
            const Disposition child_disposition = classify(std::static_pointer_cast<Node>(child));
            if ((child_disposition != Disposition::merged) && (child_disposition != Disposition::pruned)) {
                all_children_removed = false;
            }
        }
        const std::shared_ptr<Mesh> mesh = is_mergeable(node.get());
        if (mesh) {
            if (!first_source_mesh) {
                first_source_mesh = mesh;
            }
            const glm::mat4 transform = root_node_from_world * node->world_from_node();
            for (const Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = mesh_primitive.primitive->render_shape->get_geometry();
                std::size_t group = material_order.size();
                for (std::size_t i = 0, end = material_order.size(); i < end; ++i) {
                    if (material_order[i] == mesh_primitive.material) {
                        group = i;
                        break;
                    }
                }
                if (group == material_order.size()) {
                    material_order.push_back(mesh_primitive.material);
                    combined_geometries.push_back(std::make_shared<erhe::geometry::Geometry>());
                }
                combined_geometries[group]->merge_with_transform(*geometry.get(), to_geo_mat4f(transform));
                if (normal_style == erhe::primitive::Normal_style::none) {
                    normal_style = mesh_primitive.primitive->render_shape->get_normal_style();
                }
            }
            dispositions[node.get()] = Disposition::merged;
            return Disposition::merged;
        }
        if (node->get_attachments().empty() && has_children && all_children_removed) {
            // Part pose node / chain group whose whole payload was merged.
            // Attachment-less LEAVES are never pruned - zero-child markers
            // (joint pivot anchors) may be referenced from outside.
            dispositions[node.get()] = Disposition::pruned;
            return Disposition::pruned;
        }
        dispositions[node.get()] = Disposition::kept;
        return Disposition::kept;
    };
    for (const auto& child : root->get_children()) {
        if (erhe::is<Node>(child.get())) {
            classify(std::static_pointer_cast<Node>(child));
        }
    }

    // Pre-order derivation keeps parents before children in the removal
    // list, so undo's forward pass rebuilds the chains.
    for (const std::shared_ptr<Node>& node : visited_preorder) {
        const Disposition disposition = dispositions[node.get()];
        const std::shared_ptr<Node> parent = node->get_parent_node();
        const bool parent_removed =
            (parent != root) &&
            (dispositions.count(parent.get()) > 0) &&
            ((dispositions[parent.get()] == Disposition::merged) || (dispositions[parent.get()] == Disposition::pruned));
        if ((disposition == Disposition::merged) || (disposition == Disposition::pruned)) {
            target.removed_nodes.push_back(node);
            target.removed_before_parents.push_back(parent);
        } else if (parent_removed) {
            target.reparented.push_back({node, parent, {}});
        }
    }

    if (target.removed_nodes.empty()) {
        return; // nothing to bake under this segment
    }

    if (!target.mesh) {
        // Group-node target (e.g. a plant root group): give it a mesh to
        // receive the baked primitives, mirroring the sources' layer/flags.
        const std::shared_ptr<Mesh> source_mesh = first_source_mesh;
        ERHE_VERIFY(source_mesh);
        target.mesh = std::make_shared<Mesh>(fmt::format("{} merged", root->get_name()));
        target.mesh->layer_id = source_mesh->layer_id;
        target.mesh->enable_flag_bits(source_mesh->get_flag_bits());
        target.mesh_created = true;
    } else {
        target.primitives_before = target.mesh->get_primitives();
    }

    target.primitives_after = target.primitives_before;
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    for (std::size_t i = 0, end = material_order.size(); i < end; ++i) {
        combined_geometries[i]->process({.flags = flags});
        std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(combined_geometries[i]);
        const bool renderable_ok = primitive->make_renderable_mesh(m_parameters.build_info, normal_style);
        const bool raytrace_ok   = primitive->make_raytrace();
        ERHE_VERIFY(renderable_ok && raytrace_ok);
        target.primitives_after.emplace_back(primitive, material_order[i]);
    }

    m_targets.push_back(std::move(target));
}

void Merge_static_subtree_operation::execute(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_targets.empty()) {
        return;
    }

    erhe::Item_host* const item_host = m_targets.front().root->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{item_host->item_host_mutex};

    for (Target& target : m_targets) {
        // Reparent kept/boundary nodes whose parent is merged away, keeping
        // their CURRENT world pose (physics may be mid-sway; body-driven
        // nodes get their parent_from_node refreshed by the writeback every
        // frame anyway).
        for (Reparented_entry& entry : target.reparented) {
            entry.before_parent_from_node = entry.node->parent_from_node_transform();
            const glm::mat4 parent_from_node = target.root->node_from_world() * entry.node->world_from_node();
            entry.node->set_parent(target.root);
            entry.node->set_parent_from_node(parent_from_node);
        }
        for (const std::shared_ptr<erhe::scene::Node>& node : target.removed_nodes) {
            node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
        if (target.mesh_created) {
            target.root->attach(target.mesh);
        }
        erhe::raytrace::IScene* const rt_scene = target.mesh->get_rt_scene();
        target.mesh->detach_rt_from_scene();
        target.mesh->set_primitives(target.primitives_after);
        if (rt_scene != nullptr) {
            target.mesh->attach_rt_to_scene(rt_scene);
        }
        target.mesh->handle_node_transform_update();
        context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = target.mesh});
    }

    log_operations->info("{}", describe());
}

void Merge_static_subtree_operation::undo(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_targets.empty()) {
        return;
    }

    erhe::Item_host* const item_host = m_targets.front().root->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{item_host->item_host_mutex};

    for (auto it = m_targets.rbegin(); it != m_targets.rend(); ++it) {
        Target& target = *it;
        erhe::raytrace::IScene* const rt_scene = target.mesh->get_rt_scene();
        target.mesh->detach_rt_from_scene();
        target.mesh->set_primitives(target.primitives_before);
        if (rt_scene != nullptr) {
            target.mesh->attach_rt_to_scene(rt_scene);
        }
        target.mesh->handle_node_transform_update();
        if (target.mesh_created) {
            target.root->detach(target.mesh.get());
        }
        // Parents come before children in the stored depth-first order, so a
        // forward pass rebuilds the chains.
        for (std::size_t i = 0, end = target.removed_nodes.size(); i < end; ++i) {
            target.removed_nodes[i]->set_parent(target.removed_before_parents[i]);
        }
        for (const Reparented_entry& entry : target.reparented) {
            entry.node->set_parent(entry.before_parent);
            entry.node->set_parent_from_node(entry.before_parent_from_node.get_matrix());
        }
        context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = target.mesh});
    }
}

}
