#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::physics::IRigid_body_create_info;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::Node_attachment;

Node_physics::Node_physics(const Node_physics&) = default;
Node_physics& Node_physics::operator=(const Node_physics&) = default;

Node_physics::Node_physics(const Node_physics& src, erhe::for_clone)
    : Item          {src, erhe::for_clone{}}
    , markers       {}        // clone does not initially have markers
    , m_physics_world{nullptr} // clone is initially detached
    , m_create_info {src.m_create_info}
    , m_rigid_body  {}        // clone rigid body is not initially created
    , m_motion_mode {src.m_motion_mode}
{
}

Node_physics::Node_physics(const IRigid_body_create_info& create_info)
    : m_create_info{create_info}
    , m_motion_mode{create_info.motion_mode}
{
}

Node_physics::~Node_physics() noexcept
{
    set_node(nullptr);
}

void Node_physics::set_physics_world(erhe::physics::IWorld* value)
{
    if (value != nullptr) {
        ERHE_VERIFY(m_physics_world == nullptr);
    }
    m_physics_world = value;
}

auto Node_physics::get_physics_world() const -> erhe::physics::IWorld*
{
    return m_physics_world;
}

auto Node_physics::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Node_physics>(*this, erhe::for_clone{});
}

void Node_physics::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    // NOTE: This also keeps this alive is old host is only shared_ptr to it
    const auto shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());

    if (old_item_host != nullptr) {
        Scene_root* old_scene_root = static_cast<Scene_root*>(old_item_host);
        old_scene_root->unregister_node_physics(shared_this);
        m_rigid_body.reset();
    }
    if (new_item_host != nullptr) {
        Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
        auto& physics_world = new_scene_root->get_physics_world();

        log_physics->trace("making rigid body for {}", get_node()->get_name());
        create_rigid_body(physics_world);
        new_scene_root->register_node_physics(shared_this);
    }
}

auto Node_physics::get_effective_motion_mode() const -> Motion_mode
{
    // Jolt sensors must be non-static to detect overlaps with static bodies.
    return (m_create_info.is_sensor && (m_motion_mode == Motion_mode::e_static))
        ? Motion_mode::e_kinematic_non_physical
        : m_motion_mode;
}

void Node_physics::create_rigid_body(erhe::physics::IWorld& physics_world)
{
    m_create_info.motion_mode = get_effective_motion_mode();
    const erhe::scene::Node* node = get_node();
    if (node != nullptr) {
        const erhe::scene::Trs_transform& world_from_node_transform = node->world_from_node_transform();
        m_create_info.position    = world_from_node_transform.get_translation();
        m_create_info.orientation = world_from_node_transform.get_rotation();
    }
    m_rigid_body = physics_world.create_rigid_body_shared(m_create_info);
    m_rigid_body->set_owner(this);
}

void Node_physics::recreate_rigid_body()
{
    if (m_physics_world == nullptr) {
        // Not attached to a scene; the updated create info is picked up when
        // the rigid body is created at attach time.
        return;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(get_item_host());
    ERHE_VERIFY(scene_root != nullptr);
    const auto shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());
    // unregister_node_physics() tears down joint constraints referencing the
    // old body and removes it from the world; register_node_physics() adds
    // the new body and retries pending joint constraints.
    scene_root->unregister_node_physics(shared_this);
    m_rigid_body.reset();
    create_rigid_body(scene_root->get_physics_world());
    scene_root->register_node_physics(shared_this);
}

void Node_physics::before_physics_simulation()
{
    const erhe::scene::Trs_transform& world_from_node_transform = get_node()->world_from_node_transform();
    erhe::physics::Transform transform{
        glm::mat3_cast(world_from_node_transform.get_rotation()),
        world_from_node_transform.get_translation()
    };
    m_rigid_body->set_world_transform(transform);

    const auto get_transform = m_rigid_body->get_world_transform();
    const glm::vec3 get_world_position = glm::vec3{get_transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

// This is intended to be called from physics backend
void Node_physics::after_physics_simulation()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_node != nullptr);
    ERHE_VERIFY(m_rigid_body);

    const auto motion_mode = m_rigid_body->get_motion_mode();
    if (motion_mode != erhe::physics::Motion_mode::e_dynamic) {
        return;
    }

    const auto transform = m_rigid_body->get_world_transform();
    const glm::vec3 world_position = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{1.0f}, respawn_location});
        m_rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        const glm::mat4 matrix = erhe::math::create_translation<float>(respawn_location);
        get_node()->set_world_from_node(matrix);
    } else {
        get_node()->set_world_from_node(transform);
    }
}

auto Node_physics::get_rigid_body() -> IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto Node_physics::get_rigid_body() const -> const IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto Node_physics::get_collision_shape() const -> const std::shared_ptr<erhe::physics::ICollision_shape>&
{
    return m_create_info.collision_shape;
}

void Node_physics::set_collision_shape(const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape)
{
    if (m_create_info.collision_shape == collision_shape) {
        return;
    }
    m_create_info.collision_shape = collision_shape;
    recreate_rigid_body();
}

auto Node_physics::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Node_physics::set_motion_mode(Motion_mode mode)
{
    m_motion_mode = mode;
    m_create_info.motion_mode = get_effective_motion_mode();
    if (m_rigid_body) {
        m_rigid_body->set_motion_mode(m_create_info.motion_mode);
    }
}

auto Node_physics::get_physics_material() const -> const std::shared_ptr<erhe::physics::Physics_material>&
{
    return m_create_info.physics_material;
}

void Node_physics::set_physics_material(const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    m_create_info.physics_material = physics_material;
    if (m_rigid_body) {
        m_rigid_body->set_physics_material(physics_material);
    }
}

auto Node_physics::get_collision_filter() const -> const std::shared_ptr<erhe::physics::Collision_filter>&
{
    return m_create_info.collision_filter;
}

void Node_physics::set_collision_filter(const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    m_create_info.collision_filter = collision_filter;
    if (m_rigid_body) {
        m_rigid_body->set_collision_filter(collision_filter);
    }
}

auto Node_physics::is_trigger() const -> bool
{
    return m_create_info.is_sensor;
}

void Node_physics::set_trigger(const bool trigger)
{
    if (m_create_info.is_sensor == trigger) {
        return;
    }
    m_create_info.is_sensor = trigger;
    recreate_rigid_body();
}

auto Node_physics::get_gravity_factor() const -> float
{
    return m_create_info.gravity_factor;
}

void Node_physics::set_gravity_factor(const float gravity_factor)
{
    m_create_info.gravity_factor = gravity_factor;
    if (m_rigid_body && (m_rigid_body->get_motion_mode() != Motion_mode::e_static)) {
        m_rigid_body->set_gravity_factor(gravity_factor);
    }
}

auto Node_physics::get_initial_linear_velocity() const -> glm::vec3
{
    return m_create_info.linear_velocity;
}

void Node_physics::set_initial_linear_velocity(const glm::vec3& velocity)
{
    m_create_info.linear_velocity = velocity;
}

auto Node_physics::get_initial_angular_velocity() const -> glm::vec3
{
    return m_create_info.angular_velocity;
}

void Node_physics::set_initial_angular_velocity(const glm::vec3& velocity)
{
    m_create_info.angular_velocity = velocity;
}

auto Node_physics::get_center_of_mass_offset() const -> glm::vec3
{
    const std::shared_ptr<erhe::physics::ICollision_shape>& shape = m_create_info.collision_shape;
    if (!shape) {
        return glm::vec3{0.0f};
    }
    return shape->get_offset().value_or(glm::vec3{0.0f});
}

void Node_physics::set_center_of_mass_offset(const glm::vec3& offset)
{
    if (offset == get_center_of_mass_offset()) {
        return;
    }
    std::shared_ptr<erhe::physics::ICollision_shape> shape = m_create_info.collision_shape;
    if (!shape) {
        log_physics->warn("set_center_of_mass_offset() ignored: no collision shape");
        return;
    }
    if (shape->get_offset().has_value()) {
        shape = shape->get_inner_shape(); // unwrap existing offset-center-of-mass wrapper
    }
    if (offset != glm::vec3{0.0f}) {
        shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(shape, offset);
    }
    m_create_info.collision_shape = shape;
    recreate_rigid_body();
}

void Node_physics::begin_interaction()
{
    if (!m_rigid_body) {
        return;
    }
    // m_motion_mode already holds the intended mode - no need to re-read
    // from the rigid body, which may already be kinematic from a prior call.
    m_rigid_body->set_motion_mode(Motion_mode::e_kinematic_physical);
    m_rigid_body->begin_move();
}

void Node_physics::end_interaction()
{
    if (!m_rigid_body) {
        return;
    }
    m_rigid_body->set_motion_mode(get_effective_motion_mode());
    m_rigid_body->end_move();
}

void Node_physics::teleport_to_node()
{
    if (!m_rigid_body) {
        return;
    }
    const Motion_mode mode = m_rigid_body->get_motion_mode();
    // Only dynamic and kinematic-physical bodies can carry or produce kinetic energy
    // that the simulation reacts to. Static and kinematic-non-physical bodies need no
    // settling (the latter is already teleported via set_world_transform without
    // inducing velocity).
    if ((mode != Motion_mode::e_dynamic) && (mode != Motion_mode::e_kinematic_physical)) {
        return;
    }
    const erhe::scene::Trs_transform& world_from_node = get_node()->world_from_node_transform();
    m_rigid_body->teleport(
        erhe::physics::Transform{
            glm::mat3_cast(world_from_node.get_rotation()),
            world_from_node.get_translation()
        }
    );
    m_rigid_body->set_linear_velocity (glm::vec3{0.0f});
    m_rigid_body->set_angular_velocity(glm::vec3{0.0f});
    if (mode == Motion_mode::e_dynamic) {
        // Wake the body so gravity and the (possibly new) constraint take effect from
        // the placed pose instead of the body floating asleep.
        m_rigid_body->begin_move();
        m_rigid_body->end_move();
    }
}

}
