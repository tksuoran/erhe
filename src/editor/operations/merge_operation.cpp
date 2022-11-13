#include "operations/merge_operation.hpp"

#include "editor_log.hpp"
#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"

#include <memory>
#include <sstream>

namespace editor
{

// https://github.com/bulletphysics/bullet3/issues/1352
//
// The btRigidBody is aligned with the center of mass.
// You need to recompute the shift to make sure the compound shape is re-aligned.
//
// https://github.com/bulletphysics/bullet3/blob/master/examples/ExtendedTutorials/CompoundBoxes.cpp
//
// https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=11004
//
//static btTransform RigidBody_TransformCompoundRecursive(btCompoundShape* compound, btScalar mass)
//{
//    btScalar* masses = RigidBody_CalculateMasses(compound, mass);
//
//    // Recurse down the compound tree, transforming each compound and their children so that the
//    // compound is positioned at its centre of mass and with its axes aligned with those of the
//    // principal inertia tensor.
//    for (int i = 0; i < compound->getNumChildShapes(); ++i)
//    {
//        btCollisionShape* childShape = compound->getChildShape(i);
//        if (childShape->isCompound())
//        {
//            btCompoundShape* childCompound = static_cast<btCompoundShape*>(childShape);
//            btTransform childPrincipalTransform = RigidBody_TransformCompoundRecursive(childCompound, masses[i]);
//            compound->updateChildTransform(i, childPrincipalTransform * compound->getChildTransform(i)); // Transform the compound so that it is positioned at its centre of mass.
//        }
//    }
//
//    // Calculate the principal transform for the compound. This has its origin at the compound's
//    // centre of mass and its axes aligned with those of the inertia tensor.
//    btTransform principalTransform;
//    btVector3 principalInertia;
//    compound->calculatePrincipalAxisTransform(masses, principalTransform, principalInertia);
//
//    // Transform all the child shapes by the inverse of the compound's principal transform, so
//    // as to restore their world positions.
//    for (int i = 0; i < compound->getNumChildShapes(); ++i)
//    {
//        btCollisionShape* childShape = compound->getChildShape(i);
//        compound->updateChildTransform(i, principalTransform.inverse() * compound->getChildTransform(i));
//    }
//
//    return principalTransform;
//}

auto Merge_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Merge ";
    bool first = true;
    for (const auto& entry : m_sources)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            ss << ", ";
        }
        ss << entry.mesh->name();
    }
    return ss.str();
}

Merge_operation::Merge_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    // TODO count meshes in selection
    if (parameters.selection_tool->selection().size() < 2)
    {
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

    m_selection_before = parameters.selection_tool->selection();

    for (const auto& item : parameters.selection_tool->selection())
    {
        const auto& mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }
        mat4 transform;
        auto node_physics = get_physics_node(mesh.get());

        Entry source_entry{
            .mesh         = mesh,
            .node_physics = node_physics
        };

        if (first_mesh)
        {
            scene_root                = reinterpret_cast<Scene_root*>(mesh->node_data.host);
            reference_node_from_world = mesh->node_from_world();
            transform                 = mat4{1};
            first_mesh                = false;
            m_parent = mesh->parent().lock();
            m_selection_after.push_back(mesh);
            m_combined.mesh = mesh;
        }
        else
        {
            transform = reference_node_from_world * mesh->world_from_node();
        }

        if (node_physics)
        {
            auto rigid_body = node_physics->rigid_body();
            if (rigid_body)
            {
                auto collision_shape = (rigid_body != nullptr)
                    ? rigid_body->get_collision_shape()
                    : std::shared_ptr<erhe::physics::ICollision_shape>{};

                erhe::physics::Compound_child child{
                    .shape     = collision_shape,
                    .transform = erhe::physics::Transform{transform}
                };
                compound_shape_create_info.children.push_back(child);
            }
        }

        for (auto& primitive : mesh->mesh_data.primitives)
        {
            const auto& geometry = primitive.source_geometry;
            if (!geometry)
            {
                continue;
            }
            combined_geometry.merge(*geometry, transform);
            source_entry.primitives.push_back(primitive);
            if (normal_style == Normal_style::none)
            {
                normal_style = primitive.normal_style;
            }
            if (!material)
            {
                material = primitive.material;
            }
        }

        m_sources.emplace_back(source_entry);
    }

    if (m_sources.size() < 2)
    {
        m_sources.clear();
        return;
    }

    if (!compound_shape_create_info.children.empty())
    {
        ERHE_VERIFY(scene_root != nullptr);
        const auto& combined_collision_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(compound_shape_create_info);
        auto& physics_world = scene_root->physics_world();

        const erhe::physics::IRigid_body_create_info rigid_body_create_info{
            .world           = physics_world,
            .collision_shape = combined_collision_shape,
            .debug_label     = "merged" // TODO
        };

        m_combined.node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
    }

    const erhe::geometry::Geometry::Weld_settings weld_settings;
    combined_geometry.weld(weld_settings);
    combined_geometry.build_edges();

    m_combined.primitives.push_back(
        erhe::primitive::Primitive{
            .material              = material,
            .gl_primitive_geometry = make_primitive(
                combined_geometry,
                parameters.build_info,
                normal_style
            ),
            .rt_primitive_geometry = {}, // TODO
            .rt_vertex_buffer      = {}, // TODO
            .rt_index_buffer       = {}, // TODO
            .source_geometry       = std::make_shared<erhe::geometry::Geometry>(std::move(combined_geometry)),
            .normal_style          = normal_style
        }
    );
}

void Merge_operation::execute(const Operation_context&)
{
    log_operations->trace("Op Execute {}", describe());

    if (m_sources.empty())
    {
        return;
    }

    auto* const scene_root = reinterpret_cast<Scene_root*>(m_sources.front().mesh->node_data.host);
    if (scene_root == nullptr)
    {
        return;
    }
    auto& scene         = scene_root->scene();
    auto& physics_world = scene_root->physics_world();

    scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_sources)
    {
        const auto& mesh = entry.mesh;
        if (first_entry)
        {
            // For first mesh: Replace mesh primitives
            mesh->mesh_data.primitives = m_combined.primitives;
            auto old_node_physics = get_physics_node(mesh.get());
            if (old_node_physics)
            {
                remove_from_physics_world(physics_world, *old_node_physics.get());
                mesh->detach(old_node_physics.get());
            }
            if (m_combined.node_physics)
            {
                mesh->attach(m_combined.node_physics);
                add_to_physics_world(physics_world, m_combined.node_physics);
            }

            first_entry = false;
        }
        else
        {
            if (entry.node_physics)
            {
                remove_from_physics_world(physics_world, *entry.node_physics.get());
                mesh->detach(entry.node_physics.get());
            }
            scene.remove(mesh);
        }
    }
    m_parameters.selection_tool->set_selection(m_selection_after);

    scene.sanity_check();
}

void Merge_operation::undo(const Operation_context&)
{
    log_operations->trace("Op Undo {}", describe());

    if (m_sources.empty())
    {
        return;
    }

    auto* const scene_root = reinterpret_cast<Scene_root*>(
        m_sources.front().mesh->node_data.host
    );
    if (scene_root == nullptr)
    {
        return;
    }

    auto& scene         = scene_root->scene();
    auto& physics_world = scene_root->physics_world();

    ERHE_VERIFY(scene_root->layers().content() != nullptr);
    auto& layer = *scene_root->layers().content();

    scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_sources)
    {
        // For all entries:
        auto& mesh = entry.mesh;
        mesh->mesh_data.primitives = entry.primitives;

        if (first_entry)
        {
            first_entry = false;
            auto old_node_physics = get_physics_node(mesh.get());
            if (old_node_physics)
            {
                remove_from_physics_world(physics_world, *old_node_physics.get());
                mesh->detach(old_node_physics.get());
            }
            if (entry.node_physics)
            {
                mesh->attach(entry.node_physics);
                add_to_physics_world(physics_world, entry.node_physics);
            }
        }
        else
        {
            // For non-first meshes:
            if (entry.node_physics)
            {
                add_to_physics_world(physics_world, entry.node_physics);
                mesh->attach(entry.node_physics);
            }
            scene.add_to_mesh_layer(layer, mesh);
            if (m_parent != nullptr)
            {
                m_parent->attach(mesh);
            }
        }
    }
    scene.nodes_sorted = false;
    m_parameters.selection_tool->set_selection(m_selection_before);

    scene.sanity_check();
}

} // namespace editor
