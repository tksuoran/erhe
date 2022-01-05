#include "operations/merge_operation.hpp"
#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"

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
    for (const auto& entry : m_source_entries)
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

Merge_operation::Merge_operation(Context&& context)
    : m_context{std::move(context)}
{
    // TODO count meshes in selection
    if (context.selection_tool->selection().size() < 2)
    {
        return;
    }

    using namespace erhe::geometry;
    using namespace erhe::primitive;
    using namespace glm;

    erhe::geometry::Geometry combined_geometry;
    m_state_before.selection      = context.selection_tool->selection();
    m_state_after.collision_shape = erhe::physics::ICollision_shape::create_compound_shape_shared();
    m_state_after.mass = 0.0f;

    bool first_mesh                = true;
    mat4 reference_node_from_world = mat4{1};
    auto normal_style              = Normal_style::none;
    std::shared_ptr<erhe::primitive::Material> material;

    std::vector<float> child_masses;
    for (const auto& item : context.selection_tool->selection())
    {
        const auto& mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }
        mat4 transform;
        auto node_physics    = get_physics_node(mesh.get());
        auto rigid_body      = node_physics ? node_physics->rigid_body() : nullptr;
        auto collision_shape = (rigid_body != nullptr)
            ? rigid_body->get_collision_shape()
            : std::shared_ptr<erhe::physics::ICollision_shape>{};

        if (first_mesh)
        {
            reference_node_from_world = mesh->node_from_world();
            transform  = mat4{1};
            first_mesh = false;
            m_parent = mesh->parent();
            m_state_after.selection.push_back(mesh);
            m_state_before.collision_shape = collision_shape;
            m_state_before.mass            = node_physics ? node_physics->rigid_body()->get_mass()          : 0.0f;
            m_state_before.local_inertia   = node_physics ? node_physics->rigid_body()->get_local_inertia() : glm::vec3{0.0f};
        }
        else
        {
            transform = reference_node_from_world * mesh->world_from_node();
        }

        if (node_physics && (rigid_body != nullptr))
        {
            m_state_after.collision_shape->add_child_shape(
                collision_shape,
                erhe::physics::Transform{transform}
            );
            const auto child_mass = rigid_body->get_mass();
            child_masses.push_back(child_mass);
            m_state_after.mass += child_mass;
        }

        for (auto& primitive : mesh->data.primitives)
        {
            auto geometry = primitive.source_geometry;
            if (!geometry)
            {
                continue;
            }
            combined_geometry.merge(*geometry, transform);
            if (normal_style == Normal_style::none)
            {
                normal_style = primitive.normal_style;
            }
            if (!material)
            {
                material = primitive.material;
            }
        }

        m_source_entries.emplace_back(
            mesh,
            mesh->data.primitives
        );
    }

    if (m_source_entries.size() < 2)
    {
        return;
    }

    m_state_before.rigidbody_from_node = erhe::physics::Transform{};
    erhe::physics::Transform principal_axis_transform;
    m_state_after.collision_shape->calculate_principal_axis_transform(
        child_masses, 
        principal_axis_transform,
        m_state_after.local_inertia
    );

    m_state_after.rigidbody_from_node = inverse(principal_axis_transform);

    const erhe::geometry::Geometry::Weld_settings weld_settings;
    combined_geometry.weld(weld_settings);
    combined_geometry.build_edges();

    m_combined_primitive.material              = material;
    m_combined_primitive.gl_primitive_geometry = make_primitive(
        combined_geometry,
        context.build_info_set.gl,
        normal_style
    );
    //m_combined_primitive.rt_primitive_geometry
    m_combined_primitive.rt_index_buffer = {};
    m_combined_primitive.rt_primitive_geometry = {};
    m_combined_primitive.source_geometry = std::make_shared<erhe::geometry::Geometry>(std::move(combined_geometry));
    m_combined_primitive.normal_style    = normal_style;
}

void Merge_operation::execute() const
{
    m_context.scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_source_entries)
    {
        auto mesh = entry.mesh;
        if (first_entry)
        {
            // For first mesh: Replace mesh primitives
            mesh->data.primitives.resize(1);

            mesh->data.primitives.front() = m_combined_primitive;
            first_entry = false;
            auto node_physics = get_physics_node(mesh.get());
            if (node_physics)
            {
                node_physics->set_rigidbody_from_node(m_state_after.rigidbody_from_node);

                auto* const rigid_body = node_physics->rigid_body();
                rigid_body->set_collision_shape(m_state_after.collision_shape);
                rigid_body->set_mass_properties(m_state_after.mass, m_state_after.local_inertia);
            }
        }
        else
        {
            // For other meshes:
            auto node_physics = get_physics_node(mesh.get());
            if (node_physics)
            {
                remove_from_physics_world(m_context.physics_world, *node_physics.get());
                mesh->detach(node_physics.get());
            }
            mesh->unparent();
            remove_from_scene_layer(m_context.scene, m_context.layer, mesh);
        }
    }
    m_context.selection_tool->set_selection(m_state_after.selection);

    m_context.scene.sanity_check();
}

void Merge_operation::undo() const
{
    m_context.scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_source_entries)
    {
        // For all entries:
        auto mesh = entry.mesh;
        mesh->data.primitives = entry.primitives;
        if (first_entry)
        {
            first_entry = false;
            auto node_physics = get_physics_node(mesh.get());
            if (node_physics)
            {
                node_physics->set_rigidbody_from_node(m_state_before.rigidbody_from_node);

                auto* const rigid_body = node_physics->rigid_body();
                rigid_body->set_collision_shape(m_state_before.collision_shape);
                rigid_body->set_mass_properties(m_state_before.mass, m_state_before.local_inertia);
            }
        }
        else
        {
            // For non-first meshes:
            auto node_physics = get_physics_node(mesh.get());
            if (node_physics)
            {
                add_to_physics_world(m_context.physics_world, node_physics);
                mesh->attach(node_physics);
            }
            if (m_parent != nullptr)
            {
                m_parent->attach(mesh);
            }
            add_to_scene_layer(m_context.scene, m_context.layer, mesh);
        }
    }
    m_context.scene.nodes_sorted = false;
    m_context.selection_tool->set_selection(m_state_before.selection);

    m_context.scene.sanity_check();
}

} // namespace editor
