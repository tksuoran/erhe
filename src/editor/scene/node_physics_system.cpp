#include "scene/node_physics_system.hpp"

#include "editor_log.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/joint_system.hpp"
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
    observe_node_transform(entry);
    update_kinematic_physical_membership(entry);

    if ((entry.wake_on_attach == Wake_on_attach::yes) && (entry.rigid_body->get_motion_mode() == Motion_mode::e_dynamic)) {
        entry.rigid_body->begin_move();
        entry.rigid_body->end_move();
    }

    // The new body may be the missing body of a pending joint (scene load /
    // paste order); retry constraint creation.
    m_scene_root.get_joint_system().retry_pending_constraints();
}

void Node_physics_system::destroy_body(Node_physics_entry& entry)
{
    if (!entry.rigid_body) {
        return;
    }
    // Changes queued during the last simulation steps refer to this body;
    // they land before it goes, as they would have had they been applied
    // when reported.
    apply_queued_body_activations_of(*entry.rigid_body);
    // Tear down joint constraints referencing this rigid body before it leaves
    // the world; the affected joints return to the pending state.
    m_scene_root.get_joint_system().handle_rigid_body_removed(entry.rigid_body.get());
    if (entry.in_world && m_scene_root.has_physics_world()) {
        m_scene_root.get_physics_world().remove_rigid_body(entry.rigid_body.get());
    }
    entry.transform_observer.release();
    unqueue_node_pose(entry);
    entry.in_world = false;
    entry.rigid_body.reset();
    m_bodies.erase(std::remove(m_bodies.begin(), m_bodies.end(), &entry), m_bodies.end());
    update_kinematic_physical_membership(entry);
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
        set_body_motion_mode(entry, entry.create_info.motion_mode);
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
    set_body_motion_mode(*entry, Motion_mode::e_kinematic_physical);
    entry->rigid_body->begin_move();
}

void Node_physics_system::end_interaction(const erhe::scene::Node& node)
{
    Node_physics_entry* const entry = find(node);
    if ((entry == nullptr) || !entry->rigid_body) {
        return;
    }
    set_body_motion_mode(*entry, get_effective_motion_mode(*entry));
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

void Node_physics_system::observe_node_transform(Node_physics_entry& entry)
{
    entry.transform_observer.release();
    if (entry.node == nullptr) {
        return;
    }
    // The entry lives in a node-based map, so the reference stays valid until
    // the entry is erased, and the token (released with the body, destroyed
    // with the entry) never lets the callback outlive it.
    entry.transform_observer = entry.node->add_transform_observer(
        [this, &entry](erhe::scene::Xformable&) {
            if (m_writing_nodes_from_bodies) {
                return; // the body -> node writeback: the body already has this pose
            }
            queue_node_pose(entry);
        }
    );
}

void Node_physics_system::queue_node_pose(Node_physics_entry& entry)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_pending_mutex};
    if (entry.node_pose_pending) {
        return;
    }
    entry.node_pose_pending = true;
    m_pending_node_poses.push_back(&entry);
}

void Node_physics_system::unqueue_node_pose(Node_physics_entry& entry)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_pending_mutex};
    if (!entry.node_pose_pending) {
        return;
    }
    entry.node_pose_pending = false;
    m_pending_node_poses.erase(
        std::remove(m_pending_node_poses.begin(), m_pending_node_poses.end(), &entry),
        m_pending_node_poses.end()
    );
}

void Node_physics_system::set_body_motion_mode(Node_physics_entry& entry, const Motion_mode motion_mode)
{
    if (!entry.rigid_body) {
        return;
    }
    entry.rigid_body->set_motion_mode(motion_mode);
    update_kinematic_physical_membership(entry);
}

void Node_physics_system::update_kinematic_physical_membership(Node_physics_entry& entry)
{
    const bool wanted =
        entry.rigid_body &&
        (entry.rigid_body->get_motion_mode() == Motion_mode::e_kinematic_physical);
    const std::vector<Node_physics_entry*>::iterator i = std::find(m_kinematic_physical_bodies.begin(), m_kinematic_physical_bodies.end(), &entry);
    const bool listed = (i != m_kinematic_physical_bodies.end());
    if (wanted && !listed) {
        m_kinematic_physical_bodies.push_back(&entry);
    } else if (!wanted && listed) {
        m_kinematic_physical_bodies.erase(i);
    }
}

void Node_physics_system::write_body_from_node(Node_physics_entry& entry)
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

void Node_physics_system::apply_pending_node_poses()
{
    ERHE_PROFILE_FUNCTION();

    ++m_node_pose_pass;

    // Take the queue and clear the flags under the lock, so a write that
    // lands while the copy is worked on queues the entry again for the next
    // pass instead of being lost.
    m_pending_processing.clear();
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_pending_mutex};
        std::swap(m_pending_node_poses, m_pending_processing);
        for (Node_physics_entry* const entry : m_pending_processing) {
            entry->node_pose_pending = false;
        }
    }

    // Parents before children, as get_bodies() orders the writeback, so a
    // body-driven node's pose is written after its ancestor's.
    std::sort(
        m_pending_processing.begin(),
        m_pending_processing.end(),
        [](const Node_physics_entry* const lhs, const Node_physics_entry* const rhs) -> bool {
            if ((lhs->node == nullptr) || (rhs->node == nullptr)) {
                return false;
            }
            return lhs->node->get_depth() < rhs->node->get_depth();
        }
    );
    for (Node_physics_entry* const entry : m_pending_processing) {
        entry->node_pose_applied_pass = m_node_pose_pass;
        write_body_from_node(*entry);
    }
    m_pending_processing.clear();

    // A kinematic physical body is re-targeted every pass: MoveKinematic sets
    // the velocity that reaches the node pose over the step, and a body whose
    // node did not move needs the zero velocity that targeting its current
    // pose gives.
    for (Node_physics_entry* const entry : m_kinematic_physical_bodies) {
        if (entry->node_pose_applied_pass == m_node_pose_pass) {
            continue;
        }
        write_body_from_node(*entry);
    }
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

    // The node's transform observer sees these writes; the flag tells it
    // the body already holds the pose.
    ERHE_VERIFY(!m_writing_nodes_from_bodies);
    m_writing_nodes_from_bodies = true;
    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        entry.rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{1.0f}, respawn_location});
        entry.rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        entry.rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        entry.node->set_world_from_node(erhe::math::create_translation<float>(respawn_location));
    } else {
        entry.node->set_world_from_node(transform);
    }
    m_writing_nodes_from_bodies = false;
}

void Node_physics_system::on_body_activation(erhe::physics::IRigid_body& rigid_body, const Body_activation activation)
{
    if (m_in_simulation_steps) {
        m_queued_body_activations.push_back(
            Queued_body_activation{
                .rigid_body = &rigid_body,
                .activation = activation
            }
        );
        return;
    }
    apply_body_activation(rigid_body, activation);
}

void Node_physics_system::begin_simulation_steps()
{
    ERHE_VERIFY(!m_in_simulation_steps);
    m_in_simulation_steps = true;
}

void Node_physics_system::end_simulation_steps()
{
    ERHE_VERIFY(m_in_simulation_steps);
    m_in_simulation_steps = false;
}

void Node_physics_system::apply_queued_body_activations()
{
    ERHE_VERIFY(!m_in_simulation_steps);
    for (const Queued_body_activation& queued : m_queued_body_activations) {
        apply_body_activation(*queued.rigid_body, queued.activation);
    }
    m_queued_body_activations.clear();
}

void Node_physics_system::apply_queued_body_activations_of(const erhe::physics::IRigid_body& rigid_body)
{
    for (const Queued_body_activation& queued : m_queued_body_activations) {
        if (queued.rigid_body == &rigid_body) {
            apply_body_activation(*queued.rigid_body, queued.activation);
        }
    }
    m_queued_body_activations.erase(
        std::remove_if(
            m_queued_body_activations.begin(),
            m_queued_body_activations.end(),
            [&rigid_body](const Queued_body_activation& queued) { return queued.rigid_body == &rigid_body; }
        ),
        m_queued_body_activations.end()
    );
}

void Node_physics_system::apply_body_activation(erhe::physics::IRigid_body& rigid_body, const Body_activation activation)
{
    void* owner = rigid_body.get_owner();
    Node_physics_entry* entry = reinterpret_cast<Node_physics_entry*>(owner);
    if (entry == nullptr) {
        return;
    }
    erhe::scene::Node* node = entry->node;
    if (node == nullptr) {
        return;
    }
    if (activation == Body_activation::activated) {
        if (rigid_body.get_motion_mode() != Motion_mode::e_dynamic) {
            return;
        }
        // Activation events can be dispatched while the simulation is
        // paused (remove_rigid_body() drains the pending queue); the flag
        // must not be set then, or it sticks until the next resume and
        // hierarchy edits stop propagating to the node.
        if (!m_scene_root.is_physics_simulation_running()) {
            return;
        }
        node->enable_flag_bits(erhe::Item_flags::no_transform_update);
    } else {
        node->disable_flag_bits(erhe::Item_flags::no_transform_update);
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
