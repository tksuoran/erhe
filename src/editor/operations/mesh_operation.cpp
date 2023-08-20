#include "operations/mesh_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/physics/icollision_shape.hpp"
//#include "erhe/physics/iconvex_hull_collision_shape.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"

#include <sstream>

namespace editor
{

Mesh_operation::Mesh_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
}

auto Mesh_operation::describe() const -> std::string
{
    std::stringstream ss;
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
        entry.mesh->mesh_data.primitives = entry.after.primitives;

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        if (entry.after.node_physics) {
            node->attach(entry.after.node_physics);
        }

        auto old_node_raytrace = get_node_raytrace(node);
        if (old_node_raytrace) {
            node->detach(old_node_raytrace.get());
        }
        if (entry.after.node_raytrace) {
            node->attach(entry.after.node_raytrace);
        }
    }
}

void Mesh_operation::undo(Editor_context&)
{
    log_operations->trace("Op Undo {}", describe());

    for (const auto& entry : m_entries) {
        auto* node = entry.mesh->get_node();
        entry.mesh->mesh_data.primitives = entry.before.primitives;

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        if (entry.before.node_physics) {
            node->attach(entry.before.node_physics);
        }

        auto old_node_raytrace = get_node_raytrace(node);
        if (old_node_raytrace) {
            node->detach(old_node_raytrace.get());
        }
        if (entry.before.node_raytrace) {
            node->attach(entry.before.node_raytrace);
        }
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
        auto* node = as<erhe::scene::Node>(item).get();
        auto mesh = as<erhe::scene::Mesh>(item);
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
                .node_physics  = get_node_physics(node),
                .node_raytrace = get_node_raytrace(node),
                .primitives    = mesh->mesh_data.primitives
            },
        };

        for (auto& primitive : mesh->mesh_data.primitives) {
            if (!primitive.geometry_primitive->source_geometry) {
                continue;
            }
            auto after_geometry = std::make_shared<erhe::geometry::Geometry>(
                operation(*primitive.geometry_primitive->source_geometry.get())
            );
            entry.after.primitives.push_back(
                erhe::primitive::Primitive{
                    primitive.material,
                    std::make_shared<erhe::primitive::Geometry_primitive>(
                        after_geometry,
                        m_parameters.build_info,
                        primitive.geometry_primitive->normal_style
                    )
                }
            );
            entry.after.node_raytrace = std::make_shared<Node_raytrace>(entry.mesh, entry.after.primitives);
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
