#include "scene/joint_system.hpp"
#include "scene/joint.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

namespace editor {

namespace {

// Nearest self-or-ancestor node holding a rigid body (per
// KHR_physics_rigid_bodies a node "belongs" to the nearest ancestor body).
[[nodiscard]] auto find_nearest_body(const erhe::scene::Node* node) -> Node_physics_entry*
{
    Node_physics_system* const system = (node != nullptr) ? find_node_physics_system(*node) : nullptr;
    if (system == nullptr) {
        return nullptr;
    }
    while (node != nullptr) {
        Node_physics_entry* const entry = system->find(*node);
        if ((entry != nullptr) && entry->rigid_body) {
            return entry;
        }
        node = node->get_parent_node().get();
    }
    return nullptr;
}

// Rotation + translation of the node world transform as a physics transform;
// scale is ignored (the same convention as
// Node_physics_system::before_physics_simulation()).
[[nodiscard]] auto world_transform_of(const erhe::scene::Node& node) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    return erhe::physics::Transform{
        glm::mat3_cast(world_from_node.get_rotation()),
        world_from_node.get_translation()
    };
}

} // anonymous namespace

Joint_system::Joint_system(Scene_root& scene_root)
    : m_scene_root{scene_root}
{
}

Joint_system::~Joint_system() noexcept = default;

auto Joint_system::get_world() const -> erhe::physics::IWorld*
{
    return m_scene_root.has_physics_world() ? &m_scene_root.get_physics_world() : nullptr;
}

auto Joint_system::find(const Joint& joint) -> Joint_entry*
{
    for (const std::unique_ptr<Joint_entry>& entry : m_entries) {
        if (entry->joint == &joint) {
            return entry.get();
        }
    }
    return nullptr;
}

auto Joint_system::find(const Joint& joint) const -> const Joint_entry*
{
    for (const std::unique_ptr<Joint_entry>& entry : m_entries) {
        if (entry->joint == &joint) {
            return entry.get();
        }
    }
    return nullptr;
}

auto Joint_system::get_entries() const -> const std::vector<std::unique_ptr<Joint_entry>>&
{
    return m_entries;
}

auto Joint_system::get_constraint(const Joint& joint) const -> erhe::physics::IConstraint*
{
    const Joint_entry* const entry = find(joint);
    return (entry != nullptr) ? entry->constraint.get() : nullptr;
}

auto Joint_system::get_constraint_state(const Joint& joint) const -> const Joint_constraint_state*
{
    const Joint_entry* const entry = find(joint);
    return ((entry != nullptr) && entry->constraint) ? &entry->state : nullptr;
}

void Joint_system::register_joint(const std::shared_ptr<Joint>& joint)
{
    if (!joint) {
        return;
    }
    if (find(*joint.get()) != nullptr) {
        log_physics->error("Joint '{}' already registered in the scene", joint->get_name());
        return;
    }
    std::unique_ptr<Joint_entry> entry = std::make_unique<Joint_entry>();
    entry->joint      = joint.get();
    entry->joint_weak = joint;
    Joint_entry& entry_ref = *entry.get();
    m_entries.push_back(std::move(entry));
    observe_settings(entry_ref);
    // The needed rigid bodies may not exist yet (scene load / paste order);
    // when this returns false the joint stays pending and is retried from
    // Node_physics_system after the next body is created.
    static_cast<void>(try_create_constraint(entry_ref));
}

void Joint_system::unregister_joint(const std::shared_ptr<Joint>& joint)
{
    if (!joint) {
        return;
    }
    const std::vector<std::unique_ptr<Joint_entry>>::iterator i = std::find_if(
        m_entries.begin(), m_entries.end(),
        [&joint](const std::unique_ptr<Joint_entry>& entry) { return entry->joint == joint.get(); }
    );
    if (i == m_entries.end()) {
        log_physics->error("Joint '{}' not registered in the scene", joint->get_name());
        return;
    }
    destroy_constraint(*i->get());
    m_entries.erase(i);
}

void Joint_system::on_values_changed(Joint& joint)
{
    Joint_entry* const entry = find(joint);
    if (entry == nullptr) {
        return;
    }
    observe_settings(*entry);
    destroy_constraint(*entry);
    static_cast<void>(try_create_constraint(*entry));
}

void Joint_system::on_joint_active_changed(Joint& joint)
{
    Joint_entry* const entry = find(joint);
    if (entry == nullptr) {
        return;
    }
    // The derived active bit decides; try_create_constraint tests it.
    destroy_constraint(*entry);
    static_cast<void>(try_create_constraint(*entry));
}

void Joint_system::rebuild(Joint& joint)
{
    Joint_entry* const entry = find(joint);
    if (entry == nullptr) {
        return;
    }
    destroy_constraint(*entry);
    static_cast<void>(try_create_constraint(*entry));
}

auto Joint_system::is_jointed_rigid_body(const erhe::physics::IRigid_body* const rigid_body) const -> bool
{
    if (rigid_body == nullptr) {
        return false;
    }
    for (const std::unique_ptr<Joint_entry>& entry : m_entries) {
        if (!entry->constraint) {
            continue;
        }
        if ((entry->rigid_body_a == rigid_body) || (entry->rigid_body_b == rigid_body)) {
            return true;
        }
    }
    return false;
}

void Joint_system::retry_pending_constraints()
{
    for (const std::unique_ptr<Joint_entry>& entry : m_entries) {
        static_cast<void>(try_create_constraint(*entry.get()));
    }
}

void Joint_system::handle_rigid_body_removed(erhe::physics::IRigid_body* const rigid_body)
{
    for (const std::unique_ptr<Joint_entry>& entry : m_entries) {
        if (!entry->constraint) {
            continue;
        }
        if ((entry->rigid_body_a != rigid_body) && (entry->rigid_body_b != rigid_body)) {
            continue;
        }
        // The referenced body is about to leave the world; tear the
        // constraint down while the body is still valid. The joint stays
        // registered and pending.
        destroy_constraint(*entry.get());
    }
}

void Joint_system::observe_settings(Joint_entry& entry)
{
    entry.settings_observer.release();
    const std::shared_ptr<erhe::physics::Physics_joint_settings> settings = entry.joint->get_settings();
    if (!settings) {
        return;
    }
    // Only the six axes of the settings item shape the constraint. Its
    // Item_base properties (visible, active, name, ...) belong to an ancestor
    // owner type, and a rebuild re-captures the joint frames from the current
    // node poses and settles both bodies: a visibility toggle would otherwise
    // stop a swinging body dead.
    const erhe::property::Owner_type settings_owner = erhe::physics::Physics_joint_settings::property_owner_type();
    Joint* const                     joint          = entry.joint;
    entry.settings_observer = settings->add_observer(
        [this, joint, settings_owner](erhe::property::Dependency_object&, const erhe::property::Property_changed_args& args) {
            if (!erhe::property::is_owner_type_or_descendant(settings_owner, args.property.get_owner_type())) {
                return;
            }
            rebuild(*joint);
        }
    );
}

auto Joint_system::try_create_constraint(Joint_entry& entry) -> bool
{
    if (entry.constraint) {
        return true;
    }
    erhe::physics::IWorld* const world = get_world();
    if (world == nullptr) {
        return false;
    }
    Joint& joint = *entry.joint;
    if (!joint.is_active()) {
        return false; // an inactive prim is out of the simulation (X2)
    }
    const std::shared_ptr<erhe::scene::Node> node_0 = joint.get_body_0();
    if (!node_0) {
        return false; // stays pending - no first frame node (yet)
    }

    // Body A: nearest self-or-ancestor rigid body of the first frame node.
    Node_physics_entry* const node_physics_a = find_nearest_body(node_0.get());
    if (node_physics_a == nullptr) {
        return false; // stays pending - no rigid body on the frame node chain (yet)
    }
    erhe::physics::IRigid_body* const body_a = node_physics_a->rigid_body.get();
    if (body_a == nullptr) {
        return false; // stays pending - the body is not in the world yet
    }

    // Body B: nearest self-or-ancestor rigid body of the second frame node;
    // no second node (or none found) = constrain to the world.
    erhe::physics::IRigid_body*              body_b        {nullptr};
    erhe::scene::Node*                       body_b_node   {nullptr};
    Node_physics_entry*                      node_physics_b{nullptr};
    const std::shared_ptr<erhe::scene::Node> node_1 = joint.get_body_1();
    if (node_1) {
        node_physics_b = find_nearest_body(node_1.get());
        if (node_physics_b != nullptr) {
            body_b = node_physics_b->rigid_body.get();
            if (body_b == nullptr) {
                return false; // stays pending - the second body is not in the world yet
            }
            body_b_node = node_physics_b->node;
        } else if (node_1->get_item_host() != node_0->get_item_host()) {
            // The second frame node is not (yet) hosted by this scene - its
            // subtree (and its possible body) has not arrived; wait.
            return false; // stays pending
        }
        // else: the node is in this scene and has no rigid body on itself or
        // its ancestors - anchor to the world at its frame.
    }

    // Joint frames: the two frame nodes' world transforms expressed in the
    // respective body node spaces. For a world-anchored side the frame is the
    // world space frame itself.
    const erhe::physics::Transform world_from_frame_0 = world_transform_of(*node_0.get());
    erhe::physics::Six_dof_constraint_settings constraint_settings{};
    constraint_settings.rigid_body_a = body_a;
    constraint_settings.rigid_body_b = body_b;
    constraint_settings.frame_in_a   = inverse(world_transform_of(*node_physics_a->node)) * world_from_frame_0;
    if (body_b != nullptr) {
        ERHE_VERIFY(body_b_node != nullptr);
        ERHE_VERIFY(node_1);
        constraint_settings.frame_in_b = inverse(world_transform_of(*body_b_node)) * world_transform_of(*node_1.get());
    } else if (node_1) {
        constraint_settings.frame_in_b = world_transform_of(*node_1.get());
    } else {
        constraint_settings.frame_in_b = world_from_frame_0;
    }

    // The settings item states one limit and one drive per degree of freedom
    // in the layout Six_dof_constraint_settings is made of, so the mirrors
    // copy whole (doc/erhe/property_system.md section 4.22).
    const std::shared_ptr<erhe::physics::Physics_joint_settings> settings = joint.get_settings();
    if (settings) {
        constraint_settings.limits = settings->get_axis_limits();
        constraint_settings.drives = settings->get_axis_drives();
    }

    // Joint enableCollision = false: exclude the body pair before the
    // constraint joins the simulation.
    if (!joint.get_enable_collision() && (body_b != nullptr)) {
        world->set_collision_enabled(body_a, body_b, false);
        entry.collision_pair_disabled = true;
    }

    entry.constraint = erhe::physics::IConstraint::create_six_dof_constraint_shared(constraint_settings);
    world->add_constraint(entry.constraint.get());
    entry.rigid_body_a = body_a;
    entry.rigid_body_b = body_b;
    entry.state = Joint_constraint_state{
        .node_physics_a = node_physics_a,
        .node_physics_b = node_physics_b,
        .frame_in_a     = constraint_settings.frame_in_a,
        .frame_in_b     = constraint_settings.frame_in_b,
        .limits         = constraint_settings.limits
    };

    // Settle both bodies to their joint pose at rest so the freshly added
    // constraint starts from coincident frames with zero relative velocity -
    // no corrective impulse. teleport_to_node() also wakes dynamic bodies (so
    // the constraint takes effect immediately) and zeroes the kinematic
    // MoveKinematic delta so a selected (kinematic-physical) body injects no
    // velocity on the next frame.
    Node_physics_system& node_physics_system = m_scene_root.get_node_physics_system();
    if (node_physics_a->node != nullptr) {
        node_physics_system.teleport_to_node(*node_physics_a->node);
    }
    if ((node_physics_b != nullptr) && (node_physics_b->node != nullptr)) {
        node_physics_system.teleport_to_node(*node_physics_b->node);
    }

    log_physics->trace("Joint '{}': constraint created", joint.get_name());
    return true;
}

void Joint_system::destroy_constraint(Joint_entry& entry)
{
    if (!entry.constraint) {
        return;
    }
    erhe::physics::IWorld* const world = get_world();
    ERHE_VERIFY(world != nullptr);
    world->remove_constraint(entry.constraint.get());
    if (entry.collision_pair_disabled) {
        ERHE_VERIFY(entry.rigid_body_a != nullptr);
        ERHE_VERIFY(entry.rigid_body_b != nullptr);
        world->set_collision_enabled(entry.rigid_body_a, entry.rigid_body_b, true);
        entry.collision_pair_disabled = false;
    }
    entry.constraint.reset();
    entry.rigid_body_a = nullptr;
    entry.rigid_body_b = nullptr;
    entry.state = Joint_constraint_state{};
}

} // namespace editor
