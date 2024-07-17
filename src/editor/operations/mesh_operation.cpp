#include "operations/mesh_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

Mesh_operation::Mesh_operation(Mesh_operation_parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
}

auto Mesh_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << fmt::format("[{}] ", get_serial());
    bool first = true;
    for (const auto& entry : m_entries) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << entry.mesh->get_name();
    }
    return ss.str();
}

Mesh_operation::~Mesh_operation() noexcept = default;

void Mesh_operation::execute(Editor_context&)
{
    log_operations->trace("Op Execute {}", describe());

    for (const auto& entry : m_entries) {
        auto* node = entry.mesh->get_node();

        // TODO Improve physics RAII and remove this workaround
        std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
        node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        entry.mesh->set_primitives(entry.after.primitives);
        if (entry.after.node_physics) {
            node->attach(entry.after.node_physics);
        }

        node->set_parent(parent);
    }
}

void Mesh_operation::undo(Editor_context&)
{
    log_operations->trace("Op Undo {}", describe());

    for (const auto& entry : m_entries) {
        auto* node = entry.mesh->get_node();

        // TODO Improve physics RAII and remove this workaround
        std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
        node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        entry.mesh->set_primitives(entry.before.primitives);
        if (entry.before.node_physics) {
            node->attach(entry.before.node_physics);
        }

        node->set_parent(parent);
    }
}

void Mesh_operation::make_entries(
    const std::function<
        erhe::geometry::Geometry(erhe::geometry::Geometry&)
    > operation
)
{
    Selection& selection = *m_parameters.context.selection;
    const auto& selected_items = selection.get_selection();
    if (selected_items.empty()) {
        return;
    }

    const auto first_mesh = selection.get<erhe::scene::Mesh>();
    const auto first_node = selection.get<erhe::scene::Node>();
    if (!first_mesh && !first_node) {
        return;
    }

    auto* const first_node_raw = first_node ? first_node.get() : first_mesh->get_node();
    if (first_node_raw == nullptr) {
        // TODO Can this limitation be lifted?
        log_operations->error("First selected mesh does not have scene, cannot perform geometry operation");
        return;
    }

    auto* scene_root = static_cast<Scene_root*>(first_node_raw->node_data.host);
    if (scene_root == nullptr) {
        log_operations->error("First selected mesh does node not have item (scene) host");
        return;
    }

    const auto& scene = scene_root->get_scene();
    scene.sanity_check();

    for (auto& item : selected_items) {
        auto* node = std::dynamic_pointer_cast<erhe::scene::Node>(item).get();
        auto  mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh) {
            if (node != nullptr) {
                mesh = erhe::scene::get_mesh(node);
            }
        }
        if (!mesh) {
            continue;
        }
        if (node == nullptr) {
            node = mesh->get_node();
        }

        Entry entry{
            .mesh   = mesh,
            .before = {
                .node_physics = get_node_physics(node),
                .primitives   = mesh->get_primitives()
            },
        };

        for (auto& primitive : mesh->get_mutable_primitives()) {
            const auto& render_shape = primitive.render_shape;
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = render_shape->get_geometry();
            if (!geometry) {
                continue;
            }
            auto after_geometry = std::make_shared<erhe::geometry::Geometry>(
                operation(*geometry.get())
            );

            erhe::primitive::Primitive after_primitive{after_geometry, primitive.material};
            const bool renderable_ok = after_primitive.make_renderable_mesh(m_parameters.build_info, render_shape->get_normal_style());
            const bool raytrace_ok   = after_primitive.make_raytrace();
            ERHE_VERIFY(renderable_ok && raytrace_ok);
            entry.after.primitives.push_back(after_primitive);

            if (m_parameters.context.editor_settings->physics.static_enable) {
                auto collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                    reinterpret_cast<const float*>(
                        after_geometry->point_attributes().find<glm::vec3>(erhe::geometry::c_point_locations)->values.data()
                    ),
                    static_cast<int>(after_geometry->get_point_count()),
                    static_cast<int>(sizeof(glm::vec3))
                );

                const erhe::physics::IRigid_body_create_info rigid_body_create_info{
                    .collision_shape = collision_shape,
                    .debug_label     = after_geometry->name.c_str()
                };

                entry.after.node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
            }

        }
        add_entry(std::move(entry));
    }

    scene.sanity_check();
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
