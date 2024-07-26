#include "operations/merge_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/weld.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <memory>
#include <sstream>

namespace editor {

auto Merge_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << fmt::format("[{}] Merge ", get_serial());
    bool first = true;
    for (const auto& entry : m_sources) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << entry.mesh->get_name();
    }
    return ss.str();
}

Merge_operation::Merge_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    // TODO count meshes in selection
    auto& selected_items = m_parameters.context.selection->get_selection();
    if (selected_items.size() < 2) {
        return;
    }

    using erhe::primitive::Normal_style;
    using glm::mat4;

    erhe::physics::Compound_shape_create_info  compound_shape_create_info;
    erhe::geometry::Geometry                   combined_geometry;
    std::shared_ptr<erhe::primitive::Material> material;
    Scene_root* scene_root                = nullptr;
    bool        first_mesh                = true;
    mat4        reference_node_from_world = mat4{1};
    auto        normal_style              = Normal_style::none;

    m_selection_before = m_parameters.context.selection->get_selection();
    // Sorting nodes ensures first node is not child of some other node.
    // Other nodes will be detached from the scene.

    // TODO Re-parent children of nodes that will be detached to
    //      the remaining (first) node.
    std::sort(
        m_selection_before.begin(),
        m_selection_before.end(),
        [](const std::shared_ptr<erhe::Item_base>& lhs, const std::shared_ptr<erhe::Item_base>& rhs) {
            auto lhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(lhs);
            auto rhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(rhs);
            if (lhs_hierarchy && !rhs_hierarchy) {
                return true;
            }
            if (rhs_hierarchy && !lhs_hierarchy) {
                return false;
            }
            if (!lhs_hierarchy && !rhs_hierarchy) {
                return true;
            }
            return lhs_hierarchy->get_depth() < rhs_hierarchy->get_depth();
        }
    );

    for (const auto& item : m_selection_before) {
        auto shared_node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!shared_node) {
            continue;
        }
        erhe::scene::Node* node = shared_node.get();

        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node);
        if (!mesh) {
            continue;
        }

        mat4 transform;
        const std::shared_ptr<Node_physics> node_physics = get_node_physics(node);

        Entry source_entry{
            .mesh          = mesh,
            .node          = std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this()),
            .before_parent = node->get_parent_node(),
            .node_physics  = node_physics,
        };

        ERHE_VERIFY(source_entry.before_parent);

        if (first_mesh) {
            scene_root                = static_cast<Scene_root*>(node->node_data.host);
            reference_node_from_world = node->node_from_world();
            transform                 = mat4{1};
            first_mesh                = false;
            m_selection_after.push_back(shared_node);
            m_first_mesh_primitives_before = mesh->get_primitives();
        } else {
            transform = reference_node_from_world * node->world_from_node();
        }

        if (node_physics) {
            erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
            if (rigid_body != nullptr) {
                auto collision_shape = rigid_body->get_collision_shape();

                erhe::physics::Compound_child child{
                    .shape     = collision_shape,
                    .transform = erhe::physics::Transform{transform}
                };
                compound_shape_create_info.children.push_back(child);
            }
        }

        for (auto& primitive : mesh->get_mutable_primitives()) {
            const auto& shape = primitive.render_shape;
            if (!shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
            if (geometry) {
                combined_geometry.merge(*geometry, transform);
                if (normal_style == Normal_style::none) {
                    normal_style = shape->get_normal_style();
                }
                if (!material) {
                    material = primitive.material;
                }
            }
        }

        m_sources.emplace_back(source_entry);
    }

    if (m_sources.size() < 2) {
        m_sources.clear();
        return;
    }

    if (!compound_shape_create_info.children.empty()) {
        ERHE_VERIFY(scene_root != nullptr);
        const std::shared_ptr<erhe::physics::ICollision_shape> combined_collision_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(
            compound_shape_create_info
        );

        if (parameters.context.editor_settings->physics.static_enable) {
            const erhe::physics::IRigid_body_create_info rigid_body_create_info{
                .collision_shape = combined_collision_shape,
                .debug_label     = "merged", // TODO
                .motion_mode     = erhe::physics::Motion_mode::e_dynamic
            };

            m_combined_node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
        }
    }

    std::shared_ptr<erhe::geometry::Geometry> welded_geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::operation::weld(combined_geometry)
    );

    erhe::primitive::Primitive after_primitive{welded_geometry, material};
    const bool renderable_ok = after_primitive.make_renderable_mesh(parameters.build_info, normal_style);
    const bool raytrace_ok   = after_primitive.make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    m_first_mesh_primitives_after.push_back(after_primitive);
}

void Merge_operation::execute(Editor_context& context)
{
    ERHE_PROFILE_FUNCTION();

    log_operations->trace("begin Op Execute {}", describe());

    if (m_sources.empty()) {
        return;
    }

    auto* const scene_root = static_cast<Scene_root*>(m_sources.front().node->node_data.host);
    if (scene_root == nullptr) {
        return;
    }

#if !defined(NDEBUG)
    auto& scene = scene_root->get_scene();
    scene.sanity_check();
#endif

    // First node should have a mesh
    ERHE_VERIFY(m_sources.front().mesh);

    bool first_entry = true;
    for (const auto& entry : m_sources) {
        ERHE_VERIFY(entry.node != nullptr);
        erhe::scene::Node* node = entry.node.get();

        auto& mesh = entry.mesh;

        if (first_entry) {
            // TODO Improve physics RAII and remove this workaround
            std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();

            // This keeps node alive while we modify it
            std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());

            node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

            // For first mesh: Replace mesh primitives
            auto old_node_physics = get_node_physics(node);
            if (old_node_physics) {
                node->detach(old_node_physics.get());
            }
            mesh->set_primitives(m_first_mesh_primitives_after);
            if (m_combined_node_physics) {
                node->attach(m_combined_node_physics);
            }

            first_entry = false;

            node->set_parent(parent);
        } else {
            node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
    }
    context.selection->set_selection(m_selection_after);

#if !defined(NDEBUG)
    scene.sanity_check();
#endif
}

void Merge_operation::undo(Editor_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_sources.empty()) {
        return;
    }

    auto* const scene_root = static_cast<Scene_root*>(m_sources.front().node->node_data.host);
    if (scene_root == nullptr) {
        return;
    }

#if !defined(NDEBUG)
    auto& scene = scene_root->get_scene();
    scene.sanity_check();
#endif

    bool first_entry = true;
    for (const auto& entry : m_sources) {
        auto& mesh = entry.mesh;

        erhe::scene::Node* node = entry.node.get();

        if (first_entry) {
            // TODO Improve physics RAII and remove this workaround
            std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();

            // This keeps node alive while we modify it
            std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());

            node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

            first_entry = false;

            auto old_node_physics = get_node_physics(node);
            if (old_node_physics) {
                node->detach(old_node_physics.get());
            }

            mesh->set_primitives(m_first_mesh_primitives_before);

            if (entry.node_physics) {
                node->attach(entry.node_physics);
            }

            node->set_parent(parent);
        } else {
            node->set_parent(entry.before_parent);
        }
    }
    context.selection->set_selection(m_selection_before);

#if !defined(NDEBUG)
    scene.sanity_check();
#endif
}

} // namespace editor
