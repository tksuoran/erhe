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
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include <limits>

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

void apply_limit_to_axis(
    erhe::physics::Constraint_axis_limit& out,
    const erhe::physics::Joint_limit&     limit,
    const bool                            is_translation
)
{
    // Absent min / max leave that side unbounded. Jolt treats translation
    // limits of -FLT_MAX .. FLT_MAX as a free axis; rotation limits are
    // clamped by Jolt to [-pi, pi].
    const float unbounded_min = is_translation ? std::numeric_limits<float>::lowest() : -glm::pi<float>();
    const float unbounded_max = is_translation ? std::numeric_limits<float>::max  () :  glm::pi<float>();
    out.limited   = true;
    out.min       = limit.min.value_or(unbounded_min);
    out.max       = limit.max.value_or(unbounded_max);
    out.stiffness = limit.stiffness; // angular soft limits warn + hard limit in the backend
    out.damping   = limit.damping;
}

} // anonymous namespace

Node_joint::Node_joint()                             = default;
Node_joint::Node_joint(const Node_joint&)            = default;
Node_joint& Node_joint::operator=(const Node_joint&) = default;

Node_joint::Node_joint(
    const std::shared_ptr<erhe::scene::Node>&                     connected_node,
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
    const bool                                                    enable_collision
)
    : m_connected_node  {connected_node}
    , m_settings        {settings}
    , m_enable_collision{enable_collision}
{
}

Node_joint::Node_joint(const Node_joint& src, erhe::for_clone)
    : Item              {src, erhe::for_clone{}}
    , m_connected_node  {src.m_connected_node}
    , m_settings        {src.m_settings}
    , m_enable_collision{src.m_enable_collision}
    , m_physics_world   {nullptr} // clone is initially detached
    , m_constraint      {}        // clone constraint is not initially created
{
}

Node_joint::~Node_joint() noexcept
{
    set_node(nullptr);
}

auto Node_joint::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Node_joint>(*this, erhe::for_clone{});
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
    rebuild();
}

auto Node_joint::get_settings() const -> const std::shared_ptr<erhe::physics::Physics_joint_settings>&
{
    return m_settings;
}

void Node_joint::set_settings(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    if (m_settings == settings) {
        return;
    }
    m_settings = settings;
    rebuild();
}

auto Node_joint::get_enable_collision() const -> bool
{
    return m_enable_collision;
}

void Node_joint::set_enable_collision(const bool enable_collision)
{
    if (m_enable_collision == enable_collision) {
        return;
    }
    m_enable_collision = enable_collision;
    rebuild();
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
    IRigid_body*       body_b      {nullptr};
    erhe::scene::Node* body_b_node {nullptr};
    const std::shared_ptr<erhe::scene::Node> connected_node = m_connected_node.lock();
    if (connected_node) {
        const std::shared_ptr<Node_physics> node_physics_b = find_nearest_node_physics(connected_node.get());
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

    if (m_settings) {
        for (const erhe::physics::Joint_limit& limit : m_settings->limits) {
            int listed_axis_count = 0;
            for (std::size_t i = 0; i < 3; ++i) {
                if (limit.linear_axes [i]) ++listed_axis_count;
                if (limit.angular_axes[i]) ++listed_axis_count;
            }
            if (listed_axis_count > 1) {
                static bool warned_multi_axis_limit{false};
                if (!warned_multi_axis_limit) {
                    warned_multi_axis_limit = true;
                    log_physics->warn(
                        "Joint settings '{}' on node '{}': limit entry lists {} axes; radial limits are approximated per-axis",
                        m_settings->get_name(), node->get_name(), listed_axis_count
                    );
                }
            }
            for (std::size_t i = 0; i < 3; ++i) {
                if (limit.linear_axes[i]) {
                    apply_limit_to_axis(constraint_settings.limits[i], limit, true);
                }
                if (limit.angular_axes[i]) {
                    apply_limit_to_axis(constraint_settings.limits[3 + i], limit, false);
                }
            }
        }
        for (const erhe::physics::Joint_drive& drive : m_settings->drives) {
            if ((drive.axis < 0) || (drive.axis > 2)) {
                log_physics->warn(
                    "Joint settings '{}' on node '{}': drive axis {} out of range, drive ignored",
                    m_settings->get_name(), node->get_name(), drive.axis
                );
                continue;
            }
            const std::size_t axis_index =
                static_cast<std::size_t>(drive.axis) +
                ((drive.type == erhe::physics::Drive_type::e_angular) ? std::size_t{3} : std::size_t{0});
            if (drive.mode == erhe::physics::Drive_mode::e_acceleration) {
                static bool warned_acceleration_mode{false};
                if (!warned_acceleration_mode) {
                    warned_acceleration_mode = true;
                    log_physics->warn(
                        "Joint settings '{}' on node '{}': acceleration mode drives are not supported; treating as force mode",
                        m_settings->get_name(), node->get_name()
                    );
                }
            }
            erhe::physics::Constraint_axis_drive& out = constraint_settings.drives[axis_index];
            out.enabled             = true;
            out.use_position_target = drive.stiffness > 0.0f;
            out.position_target     = drive.position_target;
            out.velocity_target     = drive.velocity_target;
            out.stiffness           = drive.stiffness;
            out.damping             = drive.damping;
            out.max_force           = drive.max_force;
        }
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

    // Bodies enter the world deactivated; wake the dynamic ones so the new
    // constraint takes effect immediately.
    if (body_a->get_motion_mode() == Motion_mode::e_dynamic) {
        body_a->begin_move();
        body_a->end_move();
    }
    if ((body_b != nullptr) && (body_b->get_motion_mode() == Motion_mode::e_dynamic)) {
        body_b->begin_move();
        body_b->end_move();
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
}

}
