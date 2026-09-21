#include "scene/node_physics_system.hpp"

#include "editor_log.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace editor {

using erhe::physics::ICollision_shape;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;

Node_physics_system::Node_physics_system(Scene_root& scene_root)
    : m_scene_root{scene_root}
{
}

Node_physics_system::~Node_physics_system() noexcept
{
    // Scene_root::sever_host() unregisters every node before this runs, so the
    // bodies are already gone; what is left is shapes of dead nodes.
    for (std::pair<const erhe::scene::Node* const, Node_physics_entry>& entry : m_entries) {
        destroy_body(entry.second);
    }
    m_entries.clear();
    m_bodies.clear();
}

auto Node_physics_system::find(const erhe::scene::Node& node) const -> const Node_physics_entry*
{
    const std::unordered_map<const erhe::scene::Node*, Node_physics_entry>::const_iterator i = m_entries.find(&node);
    return (i == m_entries.end()) ? nullptr : &i->second;
}

auto Node_physics_system::find(const erhe::scene::Node& node) -> Node_physics_entry*
{
    const std::unordered_map<const erhe::scene::Node*, Node_physics_entry>::iterator i = m_entries.find(&node);
    return (i == m_entries.end()) ? nullptr : &i->second;
}

auto Node_physics_system::get_rigid_body(const erhe::scene::Node& node) const -> IRigid_body*
{
    const Node_physics_entry* const entry = find(node);
    return ((entry != nullptr) && entry->in_world) ? entry->rigid_body.get() : nullptr;
}

auto Node_physics_system::get_collision_shape(const erhe::scene::Node& node) const -> std::shared_ptr<ICollision_shape>
{
    const Node_physics_entry* const entry = find(node);
    return (entry != nullptr) ? entry->collision_shape : std::shared_ptr<ICollision_shape>{};
}

auto Node_physics_system::get_or_create(erhe::scene::Node& node) -> Node_physics_entry&
{
    const std::unordered_map<const erhe::scene::Node*, Node_physics_entry>::iterator i = m_entries.find(&node);
    if (i != m_entries.end()) {
        return i->second;
    }
    purge_expired();
    Node_physics_entry& entry = m_entries[&node];
    entry.node      = &node;
    entry.node_weak = std::static_pointer_cast<erhe::scene::Node>(node.shared_from_this());
    return entry;
}

void Node_physics_system::drop_if_empty(erhe::scene::Node& node)
{
    const std::unordered_map<const erhe::scene::Node*, Node_physics_entry>::iterator i = m_entries.find(&node);
    if (i == m_entries.end()) {
        return;
    }
    if (i->second.rigid_body || i->second.collision_shape) {
        return;
    }
    m_entries.erase(i);
}

void Node_physics_system::purge_expired()
{
    for (
        std::unordered_map<const erhe::scene::Node*, Node_physics_entry>::iterator i = m_entries.begin();
        i != m_entries.end();
    ) {
        if (i->second.node_weak.expired()) {
            destroy_body(i->second);
            i = m_entries.erase(i);
        } else {
            ++i;
        }
    }
}

auto Node_physics_system::get_effective_motion_mode(const Node_physics_entry& entry) -> Motion_mode
{
    // Jolt sensors must be non-static to detect overlaps with static bodies.
    return (entry.create_info.is_sensor && (entry.motion_mode == Motion_mode::e_static))
        ? Motion_mode::e_kinematic_non_physical
        : entry.motion_mode;
}

void Node_physics_system::refresh_create_info(const erhe::scene::Node& node, Node_physics_entry& entry)
{
    entry.create_info      = make_node_physics_create_info(node);
    entry.motion_mode      = entry.create_info.motion_mode;
    entry.create_info.collision_shape = entry.collision_shape;
    entry.create_info.motion_mode     = get_effective_motion_mode(entry);
    observe_physics_material(entry);
    observe_collision_filter(entry);
}

void Node_physics_system::create_body(erhe::scene::Node& node, Node_physics_entry& entry)
{
    if (entry.rigid_body) {
        return;
    }
    if (!m_scene_root.has_physics_world()) {
        return;
    }
    if (!carries_node_physics(node)) {
        return;
    }
    // An inactive item and everything below it is out of the simulation
    // (doc/erhe/usd_compatibility_design.md X2).
    if (!node.is_active()) {
        return;
    }
    if (node.get_item_host() != &m_scene_root) {
        return;
    }
    if (!entry.collision_shape) {
        // The key property alone is enough to make a body: the shape is the
        // convex hull of the node's own mesh, and a 0.5 box when the node has
        // no usable geometry - the same fallback the Create menu used.
        entry.collision_shape = build_shape_from_node_mesh(&node, true);
        if (!entry.collision_shape) {
            entry.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
        }
    }
    refresh_create_info(node, entry);
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    entry.create_info.position    = world_from_node.get_translation();
    entry.create_info.orientation = world_from_node.get_rotation();
    if (entry.create_info.debug_label.empty()) {
        entry.create_info.debug_label = node.get_name();
    }

    erhe::physics::IWorld& physics_world = m_scene_root.get_physics_world();
    entry.rigid_body = physics_world.create_rigid_body_shared(entry.create_info);
    entry.rigid_body->set_owner(&entry);
    physics_world.add_rigid_body(entry.rigid_body.get());
    entry.in_world = true;
    m_bodies.push_back(&entry);
    m_bodies_sorted = false;

    if ((entry.wake_on_attach == Wake_on_attach::yes) && (entry.rigid_body->get_motion_mode() == Motion_mode::e_dynamic)) {
        entry.rigid_body->begin_move();
        entry.rigid_body->end_move();
    }

    // The new body may be the missing body of a pending Node_joint (scene load
    // / paste order); retry constraint creation.
    for (const std::shared_ptr<Node_joint>& node_joint : m_scene_root.get_node_joints()) {
        static_cast<void>(node_joint->try_create_constraint());
    }
}

void Node_physics_system::destroy_body(Node_physics_entry& entry)
{
    if (!entry.rigid_body) {
        return;
    }
    // Tear down joint constraints referencing this rigid body before it leaves
    // the world; the affected joints return to the pending state.
    for (const std::shared_ptr<Node_joint>& node_joint : m_scene_root.get_node_joints()) {
        node_joint->handle_rigid_body_removed(entry.rigid_body.get());
    }
    if (entry.in_world && m_scene_root.has_physics_world()) {
        m_scene_root.get_physics_world().remove_rigid_body(entry.rigid_body.get());
    }
    entry.in_world = false;
    entry.rigid_body.reset();
    m_bodies.erase(std::remove(m_bodies.begin(), m_bodies.end(), &entry), m_bodies.end());
}

void Node_physics_system::recreate_body(erhe::scene::Node& node, Node_physics_entry& entry)
{
    if (!entry.rigid_body) {
        return;
    }
    destroy_body(entry);
    create_body(node, entry);
}

void Node_physics_system::on_node_registered(erhe::scene::Node& node)
{
    const bool carries = carries_node_physics(node);
    if (!carries && (m_entries.find(&node) == m_entries.end())) {
        return;
    }
    Node_physics_entry& entry = get_or_create(node);
    entry.node      = &node;
    entry.node_weak = std::static_pointer_cast<erhe::scene::Node>(node.shared_from_this());
    create_body(node, entry);
}

void Node_physics_system::on_node_unregistered(erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if (entry == nullptr) {
        return;
    }
    destroy_body(*entry);
    // The shape stays: an undo takes the node out and a redo puts it back, and
    // the shape built for it is not rebuilt by either.
    drop_if_empty(node);
}

void Node_physics_system::on_values_changed(erhe::scene::Node& node, const erhe::property::Dependency_property& property)
{
    const erhe::property::Dependency_property* const changed = &property;

    if (changed == Node_physics::motion_mode_property.get_ptr()) {
        if (!carries_node_physics(node)) {
            Node_physics_entry* const entry = find(node);
            if (entry != nullptr) {
                destroy_body(*entry);
                drop_if_empty(node);
            }
            return;
        }
        Node_physics_entry& entry = get_or_create(node);
        entry.node      = &node;
        entry.node_weak = std::static_pointer_cast<erhe::scene::Node>(node.shared_from_this());
        if (!entry.rigid_body) {
            // A body the user (or a tool) just asked for starts simulating at
            // once; one made while a node enters a scene stays asleep, which
            // is what quiet scene loading wants.
            entry.wake_on_attach = Wake_on_attach::yes;
            create_body(node, entry);
            return;
        }
        entry.motion_mode = node.get_value(Node_physics::motion_mode_property);
        entry.create_info.motion_mode = get_effective_motion_mode(entry);
        entry.rigid_body->set_motion_mode(entry.create_info.motion_mode);
        return;
    }

    Node_physics_entry* const entry = find(node);
    if (entry == nullptr) {
        return;
    }

    if (changed == Node_physics::is_trigger_property.get_ptr()) {
        entry->create_info.is_sensor = node.get_value(Node_physics::is_trigger_property);
        recreate_body(node, *entry);
    } else if (changed == Node_physics::mass_property.get_ptr()) {
        if (node.get_value_source(Node_physics::mass_property) == erhe::property::Value_source::default_value) {
            // No mass anywhere: back to the shape mass scaled by the material
            // density, which only a (re)creation computes.
            if (entry->create_info.mass.has_value()) {
                entry->create_info.mass.reset();
                recreate_body(node, *entry);
            }
            return;
        }
        const float mass = node.get_value(Node_physics::mass_property);
        if (entry->create_info.mass.has_value() && (mass == entry->create_info.mass.value())) {
            return;
        }
        entry->create_info.mass = mass;
        if (entry->rigid_body) {
            // Inertia scales linearly with mass for a fixed shape; keeping the
            // old inertia would leave the body tumbling as if it still had the
            // old mass.
            const float old_mass = entry->rigid_body->get_mass();
            glm::mat4 inertia = entry->rigid_body->get_local_inertia();
            if (old_mass > 0.0f) {
                inertia = glm::mat4{glm::mat3{inertia} * (mass / old_mass)};
            }
            entry->rigid_body->set_mass_properties(mass, inertia);
        }
    } else if (changed == Node_physics::gravity_factor_property.get_ptr()) {
        entry->create_info.gravity_factor = node.get_value(Node_physics::gravity_factor_property);
        if (entry->rigid_body && (entry->rigid_body->get_motion_mode() != Motion_mode::e_static)) {
            entry->rigid_body->set_gravity_factor(entry->create_info.gravity_factor);
        }
    } else if (changed == Node_physics::initial_linear_velocity_property.get_ptr()) {
        entry->create_info.linear_velocity = node.get_value(Node_physics::initial_linear_velocity_property);
    } else if (changed == Node_physics::initial_angular_velocity_property.get_ptr()) {
        entry->create_info.angular_velocity = node.get_value(Node_physics::initial_angular_velocity_property);
    } else if (changed == Node_physics::center_of_mass_offset_property.get_ptr()) {
        const glm::vec3 offset = node.get_value(Node_physics::center_of_mass_offset_property);
        std::shared_ptr<ICollision_shape> shape = entry->collision_shape;
        if (!shape) {
            return;
        }
        if (offset == shape->get_offset().value_or(glm::vec3{0.0f})) {
            return;
        }
        if (shape->get_offset().has_value()) {
            shape = shape->get_inner_shape(); // unwrap the existing wrapper
        }
        if (offset != glm::vec3{0.0f}) {
            shape = ICollision_shape::create_offset_center_of_mass_shape_shared(shape, offset);
        }
        entry->collision_shape            = shape;
        entry->create_info.collision_shape = shape;
        recreate_body(node, *entry);
    } else if (changed == Node_physics::physics_material_property.get_ptr()) {
        entry->create_info.physics_material = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Physics_material>>::from_value(
            node.get_value(Node_physics::physics_material_property)
        );
        observe_physics_material(*entry);
        reapply_physics_material(*entry);
    } else if (changed == Node_physics::collision_filter_property.get_ptr()) {
        entry->create_info.collision_filter = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Collision_filter>>::from_value(
            node.get_value(Node_physics::collision_filter_property)
        );
        observe_collision_filter(*entry);
        reapply_collision_filter(*entry);
    }
}

void Node_physics_system::on_node_active_changed(erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if (entry == nullptr) {
        if (!carries_node_physics(node) || !node.is_active()) {
            return;
        }
        Node_physics_entry& new_entry = get_or_create(node);
        create_body(node, new_entry);
        return;
    }
    if (node.is_active()) {
        create_body(node, *entry);
    } else {
        destroy_body(*entry);
    }
}

void Node_physics_system::set_collision_shape(erhe::scene::Node& node, const std::shared_ptr<ICollision_shape>& collision_shape)
{
    Node_physics_entry& entry = get_or_create(node);
    // The effective center-of-mass offset stays what the node says: the new
    // shape gets the wrapper the old one carried.
    const glm::vec3 offset = node.get_value(Node_physics::center_of_mass_offset_property);
    const std::shared_ptr<ICollision_shape> wrapped = ((offset != glm::vec3{0.0f}) && collision_shape)
        ? ICollision_shape::create_offset_center_of_mass_shape_shared(collision_shape, offset)
        : collision_shape;
    if (entry.collision_shape == wrapped) {
        return;
    }
    entry.collision_shape             = wrapped;
    entry.create_info.collision_shape = wrapped;
    if (entry.rigid_body) {
        recreate_body(node, entry);
    } else {
        create_body(node, entry);
    }
    drop_if_empty(node);
}

void Node_physics_system::set_wake_on_attach(erhe::scene::Node& node, const Wake_on_attach wake_on_attach)
{
    get_or_create(node).wake_on_attach = wake_on_attach;
    drop_if_empty(node);
}

void Node_physics_system::begin_interaction(const erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if ((entry == nullptr) || !entry->rigid_body) {
        return;
    }
    // entry->motion_mode already holds the intended mode - no need to re-read
    // from the rigid body, which may already be kinematic from a prior call.
    entry->rigid_body->set_motion_mode(Motion_mode::e_kinematic_physical);
    entry->rigid_body->begin_move();
}

void Node_physics_system::end_interaction(const erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if ((entry == nullptr) || !entry->rigid_body) {
        return;
    }
    entry->rigid_body->set_motion_mode(get_effective_motion_mode(*entry));
    entry->rigid_body->end_move();
}

void Node_physics_system::teleport_to_node(const erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if ((entry == nullptr) || !entry->rigid_body) {
        return;
    }
    const Motion_mode mode = entry->rigid_body->get_motion_mode();
    // Only dynamic and kinematic-physical bodies can carry or produce kinetic
    // energy that the simulation reacts to.
    if ((mode != Motion_mode::e_dynamic) && (mode != Motion_mode::e_kinematic_physical)) {
        return;
    }
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    entry->rigid_body->teleport(
        erhe::physics::Transform{
            glm::mat3_cast(world_from_node.get_rotation()),
            world_from_node.get_translation()
        }
    );
    entry->rigid_body->set_linear_velocity (glm::vec3{0.0f});
    entry->rigid_body->set_angular_velocity(glm::vec3{0.0f});
    if (mode == Motion_mode::e_dynamic) {
        // Wake the body so gravity and the (possibly new) constraint take
        // effect from the placed pose instead of the body floating asleep.
        entry->rigid_body->begin_move();
        entry->rigid_body->end_move();
    }
}

auto Node_physics_system::get_bodies() -> const std::vector<Node_physics_entry*>&
{
    if (!m_bodies_sorted) {
        // Parent transforms are updated before child nodes.
        std::sort(
            m_bodies.begin(),
            m_bodies.end(),
            [](const Node_physics_entry* const lhs, const Node_physics_entry* const rhs) -> bool {
                // Every entry with a body names its node, so the null guard
                // only has to keep the ordering well formed.
                if ((lhs->node == nullptr) || (rhs->node == nullptr)) {
                    return false;
                }
                return lhs->node->get_depth() < rhs->node->get_depth();
            }
        );
        m_bodies_sorted = true;
    }
    return m_bodies;
}

void Node_physics_system::before_physics_simulation(Node_physics_entry& entry)
{
    if (!entry.rigid_body || (entry.node == nullptr)) {
        return;
    }
    const erhe::scene::Trs_transform& world_from_node = entry.node->world_from_node_transform();
    entry.rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3_cast(world_from_node.get_rotation()),
            world_from_node.get_translation()
        }
    );
}

void Node_physics_system::after_physics_simulation(Node_physics_entry& entry)
{
    ERHE_PROFILE_FUNCTION();

    if (!entry.rigid_body || (entry.node == nullptr)) {
        return;
    }
    if (entry.rigid_body->get_motion_mode() != Motion_mode::e_dynamic) {
        return;
    }

    const glm::mat4 transform      = entry.rigid_body->get_world_transform();
    const glm::vec3 world_position = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        entry.rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{1.0f}, respawn_location});
        entry.rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        entry.rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        entry.node->set_world_from_node(erhe::math::create_translation<float>(respawn_location));
    } else {
        entry.node->set_world_from_node(transform);
    }
}

void Node_physics_system::observe_physics_material(Node_physics_entry& entry)
{
    entry.physics_material_observer.release();
    if (entry.create_info.physics_material) {
        entry.physics_material_observer = entry.create_info.physics_material->add_observer(
            [this, &entry](erhe::property::Dependency_object&, const erhe::property::Property_changed_args&) {
                reapply_physics_material(entry);
            }
        );
    }
}

void Node_physics_system::observe_collision_filter(Node_physics_entry& entry)
{
    entry.collision_filter_observer.release();
    if (entry.create_info.collision_filter) {
        entry.collision_filter_observer = entry.create_info.collision_filter->add_observer(
            [this, &entry](erhe::property::Dependency_object&, const erhe::property::Property_changed_args&) {
                reapply_collision_filter(entry);
            }
        );
    }
}

void Node_physics_system::reapply_physics_material(Node_physics_entry& entry)
{
    if (entry.rigid_body) {
        entry.rigid_body->set_physics_material(entry.create_info.physics_material);
    }
}

void Node_physics_system::reapply_collision_filter(Node_physics_entry& entry)
{
    if (entry.rigid_body) {
        entry.rigid_body->set_collision_filter(entry.create_info.collision_filter);
    }
}

auto find_node_physics_system(const erhe::scene::Node& node) -> Node_physics_system*
{
    erhe::Item_host* const item_host = node.get_item_host();
    if (item_host == nullptr) {
        return nullptr;
    }
    return &static_cast<Scene_root*>(item_host)->get_node_physics_system();
}

auto get_node_collision_shape(const erhe::scene::Node& node) -> std::shared_ptr<ICollision_shape>
{
    Node_physics_system* const system = find_node_physics_system(node);
    return (system != nullptr) ? system->get_collision_shape(node) : std::shared_ptr<ICollision_shape>{};
}

auto get_node_rigid_body(const erhe::scene::Node& node) -> IRigid_body*
{
    Node_physics_system* const system = find_node_physics_system(node);
    return (system != nullptr) ? system->get_rigid_body(node) : nullptr;
}

void set_node_collision_shape(erhe::scene::Node& node, const std::shared_ptr<ICollision_shape>& collision_shape)
{
    Node_physics_system* const system = find_node_physics_system(node);
    if (system == nullptr) {
        return;
    }
    system->set_collision_shape(node, collision_shape);
}

} // namespace editor
