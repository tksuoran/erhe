#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace editor {

using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::Node_attachment;

namespace {

// Nearest self-or-ancestor Node_physics of node (per KHR_physics_rigid_bodies
// a node "belongs" to the nearest ancestor body).
[[nodiscard]] auto find_nearest_node_physics(const erhe::scene::Node* node) -> std::shared_ptr<Node_physics>
{
    while (node != nullptr) {
        std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node);
        if (node_physics) {
            return node_physics;
        }
        node = node->get_parent_node().get();
    }
    return {};
}

// Rotation + translation of the node world transform as a physics transform;
// scale is ignored (the same convention as Node_physics::before_physics_simulation()).
[[nodiscard]] auto world_transform_of(const erhe::scene::Node& node) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    return erhe::physics::Transform{
        glm::mat3_cast(world_from_node.get_rotation()),
        world_from_node.get_translation()
    };
}

using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using Node_traits     = erhe::property::Member_value_traits<std::shared_ptr<erhe::scene::Node>>;
using Settings_traits = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Physics_joint_settings>>;

const erhe::property::Owner_type c_joint_owner = Node_joint::property_owner_type();
constexpr std::string_view      c_joint_group = "Joint";

} // anonymous namespace

// Section 4.17: the connected node bridged over the weak member (no strong
// node-to-node reference), the settings and the collision flag in the
// entry store, inheriting.
const Property<Object_reference> Node_joint::connected_node_property = Property<Object_reference>::register_property(
    "connected_node", c_joint_owner,
    Property_metadata{
        .ui     = Property_ui{.group = c_joint_group, .tooltip = "Node whose nearest rigid body is body B; none constrains to the world. The joint's own node is refused.", .label = "Connected Node", .reference_item_types = erhe::Item_type::xformable},
        .bridge = erhe::property::Property_bridge{
            .get = [](const erhe::property::Dependency_object& object) -> Property_value {
                return Node_traits::to_value(static_cast<const Node_joint&>(object).get_connected_node());
            },
            .set = [](erhe::property::Dependency_object& object, const Property_value& value) {
                Node_joint& joint = static_cast<Node_joint&>(object);
                const std::shared_ptr<erhe::scene::Node> node = Node_traits::from_value(value);
                if (node && (node.get() == joint.get_node())) {
                    log_physics->warn("joint on '{}': a joint cannot connect to its own node", node->get_name());
                    return;
                }
                joint.set_connected_node(node);
            }
        }
    },
    Node_traits::validate
);
const Property<Object_reference> Node_joint::joint_settings_property = Property<Object_reference>::register_property(
    "joint_settings", c_joint_owner,
    Property_metadata{.inherits = true, .ui = Property_ui{.group = c_joint_group, .tooltip = "Shared six-dof limits and drives the constraint is built from", .label = "Joint Settings", .reference_item_types = erhe::Item_type::physics_joint_settings}},
    Settings_traits::validate
);
const Property<bool> Node_joint::enable_collision_property = Property<bool>::register_property(
    "enable_collision", c_joint_owner,
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_joint_group, .tooltip = "Let the two connected bodies collide with each other", .label = "Enable Collision"}}
);

Node_joint::Node_joint() = default;

// Hand-written: the observer token is not copyable, and a copy observes the
// settings item it has just taken over.
Node_joint::Node_joint(const Node_joint& src)
    : Item              {src}
    , m_connected_node  {src.m_connected_node}
    , m_settings        {src.m_settings}
    , m_enable_collision{src.m_enable_collision}
    , m_physics_world   {src.m_physics_world}
    , m_constraint      {src.m_constraint}
    , m_rigid_body_a    {src.m_rigid_body_a}
    , m_rigid_body_b    {src.m_rigid_body_b}
    , m_collision_pair_disabled{src.m_collision_pair_disabled}
    , m_constraint_state{src.m_constraint_state}
{
    observe_settings();
}

Node_joint& Node_joint::operator=(const Node_joint& src)
{
    Item::operator=(src);
    m_connected_node   = src.m_connected_node;
    m_settings         = src.m_settings;
    m_enable_collision = src.m_enable_collision;
    m_physics_world    = src.m_physics_world;
    m_constraint       = src.m_constraint;
    m_rigid_body_a     = src.m_rigid_body_a;
    m_rigid_body_b     = src.m_rigid_body_b;
    m_collision_pair_disabled = src.m_collision_pair_disabled;
    m_constraint_state = src.m_constraint_state;
    observe_settings();
    return *this;
}

Node_joint::Node_joint(
    const std::shared_ptr<erhe::scene::Node>&                     connected_node,
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
    const bool                                                    enable_collision
)
    : m_connected_node  {connected_node}
    , m_settings        {settings}
    , m_enable_collision{enable_collision}
{
    // Local values only where the arguments differ from the property
    // defaults, so a default-constructed joint stays open to a holder
    // (a set_value here reaches on_property_changed, whose rebuild is a
    // no-op while detached).
    if (settings)         { set_value(joint_settings_property, Settings_traits::to_value(settings)); }
    if (enable_collision) { set_value(enable_collision_property, true); }
    observe_settings();
}

Node_joint::Node_joint(const Node_joint& src, erhe::for_clone)
    : Item              {src, erhe::for_clone{}}
    , m_connected_node  {src.m_connected_node}
    , m_settings        {src.m_settings}
    , m_enable_collision{src.m_enable_collision}
    , m_physics_world   {nullptr} // clone is initially detached
    , m_constraint      {}        // clone constraint is not initially created
{
    observe_settings();
}

Node_joint::~Node_joint() noexcept
{
    set_node(nullptr);
}

void Node_joint::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    // NOTE: This also keeps this alive if old host is the only shared_ptr to it
    const auto shared_this = std::static_pointer_cast<Node_joint>(shared_from_this());

    if (old_item_host != nullptr) {
        Scene_root* old_scene_root = static_cast<Scene_root*>(old_item_host);
        destroy_constraint();
        old_scene_root->unregister_node_joint(shared_this);
    }
    if (new_item_host != nullptr) {
        Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
        new_scene_root->register_node_joint(shared_this);
    }
}

auto Node_joint::get_connected_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_connected_node.lock();
}

void Node_joint::set_connected_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_connected_node.lock() == node) {
        return;
    }
    m_connected_node = node;
    invalidate_dependents(connected_node_property.get()); // bridged storage changed outside set_value (D22)
    rebuild();
}

void Node_joint::on_property_changed(const erhe::property::Property_changed_args& args)
{
    // Only the joint's own entry-stored values shape the constraint. The
    // properties the joint inherits from Item_base (visible, active, name,
    // ...) pass the owner-type test too, and a rebuild re-captures the joint
    // frames from the current node poses and teleports both bodies to rest:
    // toggling a swinging ball's visibility stopped it dead.
    // connected_node is bridged: set_connected_node already rebuilt.
    if (
        (&args.property != joint_settings_property.get_ptr()) &&
        (&args.property != enable_collision_property.get_ptr())
    ) {
        return;
    }
    refresh_mirror();
    observe_settings();
    rebuild();
}

void Node_joint::refresh_mirror()
{
    m_settings         = Settings_traits::from_value(get_value(joint_settings_property));
    m_enable_collision = get_value(enable_collision_property);
}

void Node_joint::observe_settings()
{
    m_settings_observer.release();
    if (!m_settings) {
        return;
    }
    // Only the six axes of the settings item shape the constraint. Its
    // Item_base properties (visible, active, name, ...) belong to an ancestor
    // owner type, and a rebuild re-captures the joint frames from the current
    // node poses and teleports both bodies to rest: a visibility toggle would
    // otherwise stop a swinging body dead.
    const erhe::property::Owner_type settings_owner = erhe::physics::Physics_joint_settings::property_owner_type();
    m_settings_observer = m_settings->add_observer(
        [this, settings_owner](erhe::property::Dependency_object&, const erhe::property::Property_changed_args& args) {
            if (!erhe::property::is_owner_type_or_descendant(settings_owner, args.property.get_owner_type())) {
                return;
            }
            rebuild();
        }
    );
}

auto Node_joint::get_settings() const -> const std::shared_ptr<erhe::physics::Physics_joint_settings>&
{
    return m_settings;
}

void Node_joint::set_settings(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    set_value(joint_settings_property, Settings_traits::to_value(settings));
}

auto Node_joint::get_enable_collision() const -> bool
{
    return m_enable_collision;
}

void Node_joint::set_enable_collision(const bool enable_collision)
{
    set_value(enable_collision_property, enable_collision);
}

auto Node_joint::get_constraint() const -> erhe::physics::IConstraint*
{
    return m_constraint.get();
}

void Node_joint::rebuild()
{
    destroy_constraint();
    static_cast<void>(try_create_constraint());
}

void Node_joint::set_physics_world(erhe::physics::IWorld* value)
{
    if (value != nullptr) {
        ERHE_VERIFY(m_physics_world == nullptr);
    }
    m_physics_world = value;
}

auto Node_joint::constrains_rigid_body(const erhe::physics::IRigid_body* const rigid_body) const -> bool
{
    if (!m_constraint || (rigid_body == nullptr)) {
        return false;
    }
    return (m_rigid_body_a == rigid_body) || (m_rigid_body_b == rigid_body);
}

auto Node_joint::get_constraint_state() const -> const Node_joint_constraint_state*
{
    return m_constraint ? &m_constraint_state : nullptr;
}

void Node_joint::handle_rigid_body_removed(erhe::physics::IRigid_body* rigid_body)
{
    if (!m_constraint) {
        return;
    }
    if ((m_rigid_body_a != rigid_body) && (m_rigid_body_b != rigid_body)) {
        return;
    }
    // The referenced body is about to leave the world; tear down the
    // constraint while the body is still valid. The joint stays registered
    // and pending: Scene_root::register_node_physics() retries it when a
    // rigid body becomes available again.
    destroy_constraint();
}

auto Node_joint::try_create_constraint() -> bool
{
    if (m_constraint) {
        return true;
    }
    if (m_physics_world == nullptr) {
        return false;
    }
    erhe::scene::Node* node = get_node();
    if (node == nullptr) {
        return false;
    }

    // Body A: nearest self-or-ancestor rigid body of the joint node.
    const std::shared_ptr<Node_physics> node_physics_a = find_nearest_node_physics(node);
    if (!node_physics_a) {
        return false; // stays pending - no Node_physics on the joint node chain (yet)
    }
    IRigid_body* const body_a = node_physics_a->get_rigid_body();
    if (body_a == nullptr) {
        return false; // stays pending - Node_physics exists but is not registered yet
    }

    // Body B: nearest self-or-ancestor rigid body of the connected node;
    // no connected node (or none found) = constrain to the world.
    IRigid_body*                  body_b      {nullptr};
    erhe::scene::Node*            body_b_node {nullptr};
    std::shared_ptr<Node_physics> node_physics_b;
    const std::shared_ptr<erhe::scene::Node> connected_node = m_connected_node.lock();
    if (connected_node) {
        node_physics_b = find_nearest_node_physics(connected_node.get());
        if (node_physics_b) {
            body_b = node_physics_b->get_rigid_body();
            if (body_b == nullptr) {
                return false; // stays pending - connected body exists but is not registered yet
            }
            body_b_node = node_physics_b->get_node();
        } else if (connected_node->get_item_host() != node->get_item_host()) {
            // The connected node is not (yet) hosted by this scene - its
            // subtree (and possible Node_physics) has not arrived; wait.
            return false; // stays pending
        }
        // else: the connected node is in this scene and has no rigid body on
        // itself or its ancestors - anchor to the world at its frame.
    }

    // Joint frames: the joint / connected node world transforms expressed in
    // the respective body node spaces. For a world-anchored side the frame is
    // the world space frame itself.
    const erhe::physics::Transform world_from_joint = world_transform_of(*node);
    erhe::physics::Six_dof_constraint_settings constraint_settings{};
    constraint_settings.rigid_body_a = body_a;
    constraint_settings.rigid_body_b = body_b;
    constraint_settings.frame_in_a   = inverse(world_transform_of(*node_physics_a->get_node())) * world_from_joint;
    if (body_b != nullptr) {
        ERHE_VERIFY(body_b_node != nullptr);
        ERHE_VERIFY(connected_node);
        constraint_settings.frame_in_b = inverse(world_transform_of(*body_b_node)) * world_transform_of(*connected_node);
    } else if (connected_node) {
        constraint_settings.frame_in_b = world_transform_of(*connected_node);
    } else {
        constraint_settings.frame_in_b = world_from_joint;
    }

    // The settings item states one limit and one drive per degree of freedom
    // in the layout Six_dof_constraint_settings is made of, so the mirrors
    // copy whole (doc/erhe/property_system.md section 4.22).
    if (m_settings) {
        constraint_settings.limits = m_settings->get_axis_limits();
        constraint_settings.drives = m_settings->get_axis_drives();
    }

    // Joint enableCollision = false: exclude the body pair before the
    // constraint joins the simulation.
    if (!m_enable_collision && (body_b != nullptr)) {
        m_physics_world->set_collision_enabled(body_a, body_b, false);
        m_collision_pair_disabled = true;
    }

    m_constraint = erhe::physics::IConstraint::create_six_dof_constraint_shared(constraint_settings);
    m_physics_world->add_constraint(m_constraint.get());
    m_rigid_body_a = body_a;
    m_rigid_body_b = body_b;
    m_constraint_state = Node_joint_constraint_state{
        .node_physics_a = node_physics_a.get(),
        .node_physics_b = node_physics_b.get(),
        .frame_in_a     = constraint_settings.frame_in_a,
        .frame_in_b     = constraint_settings.frame_in_b,
        .limits         = constraint_settings.limits
    };

    // Settle both bodies to their joint pose at rest so the freshly added constraint
    // starts from coincident frames with zero relative velocity - no corrective
    // impulse. teleport_to_node() also wakes dynamic bodies (so the constraint takes
    // effect immediately, replacing the previous begin_move/end_move) and zeroes the
    // kinematic MoveKinematic delta so a selected (kinematic-physical) body injects no
    // velocity on the next frame.
    node_physics_a->teleport_to_node();
    if (node_physics_b) {
        node_physics_b->teleport_to_node();
    }

    log_physics->trace("Node_joint '{}': constraint created", get_name());
    return true;
}

void Node_joint::destroy_constraint()
{
    if (!m_constraint) {
        return;
    }
    ERHE_VERIFY(m_physics_world != nullptr);
    m_physics_world->remove_constraint(m_constraint.get());
    if (m_collision_pair_disabled) {
        ERHE_VERIFY(m_rigid_body_a != nullptr);
        ERHE_VERIFY(m_rigid_body_b != nullptr);
        m_physics_world->set_collision_enabled(m_rigid_body_a, m_rigid_body_b, true);
        m_collision_pair_disabled = false;
    }
    m_constraint.reset();
    m_rigid_body_a = nullptr;
    m_rigid_body_b = nullptr;
    m_constraint_state = Node_joint_constraint_state{};
}

}
