#include "erhe_physics/box3d/box3d_rigid_body.hpp"
#include "erhe_physics/box3d/box3d_world.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_physics/physics_material.hpp"

#include <glm/gtc/quaternion.hpp>

namespace erhe::physics {

IRigid_body::~IRigid_body() noexcept
{
}

namespace {

[[nodiscard]] auto to_box3d_body_type(const Motion_mode motion_mode) -> b3BodyType
{
    switch (motion_mode) {
        case Motion_mode::e_static:                 return b3_staticBody;
        // Both kinematic modes are Box3D kinematic bodies; they differ in how
        // set_world_transform() moves them (see set_world_transform below).
        case Motion_mode::e_kinematic_non_physical: return b3_kinematicBody;
        case Motion_mode::e_kinematic_physical:     return b3_kinematicBody;
        default:                                    return b3_dynamicBody;
    }
}

// The time step b3Body_SetTargetTransform converts a pose delta into a velocity
// over. This mirrors the Jolt backend, which passes a fixed 1/30 to
// MoveKinematic for the same reason: the caller does not supply a step here.
constexpr float kinematic_target_time_step = 1.0f / 30.0f;

} // anonymous namespace

Box3d_rigid_body::Box3d_rigid_body(Box3d_world& world, const IRigid_body_create_info& create_info)
    : m_world           {world}
    , m_collision_shape {create_info.collision_shape}
    , m_physics_material{create_info.physics_material}
    , m_collision_filter{create_info.collision_filter}
    , m_debug_label     {create_info.debug_label}
    , m_motion_mode     {create_info.motion_mode}
    , m_is_sensor       {create_info.is_sensor}
{
    const Box3d_collision_shape* shape = static_cast<const Box3d_collision_shape*>(m_collision_shape.get());
    if (shape == nullptr) {
        log_physics->error("box3d body '{}': created without a collision shape", m_debug_label);
        return;
    }

    // Box3D cannot derive mass from a triangle mesh (there is no
    // b3ComputeMeshMass), so a dynamic body containing one would simulate with
    // no inertia. Report it and create the body static instead of letting it
    // misbehave silently.
    if ((m_motion_mode == Motion_mode::e_dynamic) && shape->contains_mesh()) {
        log_physics->error(
            "box3d body '{}': dynamic body with a triangle mesh shape is not supported "
            "(Box3D cannot compute mesh mass); creating it as static",
            m_debug_label
        );
        m_motion_mode = Motion_mode::e_static;
    }

    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type            = to_box3d_body_type(m_motion_mode);
    body_def.position        = to_box3d(create_info.position);
    body_def.rotation        = to_box3d(create_info.orientation);
    body_def.linearVelocity  = to_box3d(create_info.linear_velocity);
    body_def.angularVelocity = to_box3d(create_info.angular_velocity);
    body_def.linearDamping   = create_info.linear_damping;
    body_def.angularDamping  = create_info.angular_damping;
    body_def.gravityScale    = create_info.gravity_factor;
    body_def.enableSleep     = true;
    body_def.userData        = this;
    // erhe creates a body and adds it to the world as two separate steps, while
    // b3CreateBody puts it in the world immediately. Start disabled so an
    // un-added body does not simulate; add_rigid_body() enables it.
    body_def.isEnabled       = false;
    if (!m_debug_label.empty()) {
        body_def.name = m_debug_label.c_str();
    }

    m_body     = b3CreateBody(m_world.get_box3d_world(), &body_def);
    m_is_valid = true;

    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.density                = create_info.density.value_or(1.0f);
    shape_def.baseMaterial.friction  = create_info.friction;
    shape_def.baseMaterial.restitution = create_info.restitution;
    shape_def.isSensor               = m_is_sensor;
    // Box3D only reports sensor overlaps when the flag is set on BOTH shapes
    // (src/sensor.c), and it defaults to false even for sensors, so every shape
    // opts in or triggers silently never fire.
    shape_def.enableSensorEvents     = true;
    shape_def.updateBodyMass         = false; // mass is applied once, after all shapes are attached

    attach_shapes(create_info, shape_def);
    apply_mass(create_info);
}

void Box3d_rigid_body::attach_shapes(const IRigid_body_create_info& create_info, b3ShapeDef& shape_def)
{
    const Box3d_collision_shape* shape = static_cast<const Box3d_collision_shape*>(m_collision_shape.get());

    Shape_attach_context context{};
    context.body          = m_body;
    context.shape_def     = &shape_def;
    context.shape_ids     = &m_shape_ids;
    context.derived_hulls = &m_derived_hulls;
    context.primitives    = &m_primitives;
    context.debug_label   = m_debug_label.empty() ? "<unnamed>" : m_debug_label.c_str();

    shape->attach_to_body(context, b3Transform_identity, glm::vec3{1.0f});

    if (context.has_center_of_mass_offset) {
        // Box3D carries the center of mass on the body, so the wrapper's offset
        // is applied here, after all shapes exist and their mass is known.
        b3Body_ApplyMassFromShapes(m_body);
        b3MassData mass_data = b3Body_GetMassData(m_body);
        mass_data.center = to_box3d(from_box3d(mass_data.center) + context.center_of_mass_offset);
        b3Body_SetMassData(m_body, mass_data);
    }

    if (!create_info.enable_collisions) {
        // A body that must not collide with anything is expressed as a filter
        // that matches nothing, rather than by removing its shapes (which would
        // also remove its mass and make it invisible to queries).
        b3Filter filter = b3DefaultFilter();
        filter.maskBits = 0;
        for (const b3ShapeId shape_id : m_shape_ids) {
            b3Shape_SetFilter(shape_id, filter, false);
        }
    }
}

void Box3d_rigid_body::apply_mass(const IRigid_body_create_info& create_info)
{
    if (m_shape_ids.empty()) {
        return;
    }

    // Shape density was set from create_info.density, so this already yields
    // the density-derived mass.
    b3Body_ApplyMassFromShapes(m_body);
    b3MassData mass_data = b3Body_GetMassData(m_body);

    // KHR_physics_rigid_bodies convention: an explicitly provided mass of 0
    // means infinite mass.
    const bool infinite_mass = create_info.mass.has_value() && (create_info.mass.value() == 0.0f);
    if (infinite_mass) {
        mass_data.mass    = 0.0f;
        mass_data.inertia = b3Matrix3{b3Vec3_zero, b3Vec3_zero, b3Vec3_zero};
        b3Body_SetMassData(m_body, mass_data);
        return;
    }

    if (create_info.mass.has_value() && (mass_data.mass > 0.0f)) {
        // The inertia tensor is linear in mass at fixed geometry.
        const float factor = create_info.mass.value() / mass_data.mass;
        mass_data.mass    = create_info.mass.value();
        mass_data.inertia = to_box3d(from_box3d(mass_data.inertia) * factor);
    } else if ((mass_data.mass <= 0.0f) && (m_motion_mode == Motion_mode::e_dynamic)) {
        // A shape that cannot report a mass (an empty shape, or a mesh) would
        // leave a dynamic body with no inertia. Match the Jolt backend and fall
        // back to unit mass rather than producing a body the solver cannot move.
        log_physics->warn(
            "box3d body '{}': collision shape reports no mass; falling back to mass 1",
            m_debug_label
        );
        mass_data.mass    = 1.0f;
        mass_data.inertia = to_box3d(glm::mat3{1.0f});
    }

    if (create_info.inertia_override.has_value()) {
        mass_data.inertia = inertia_to_box3d(create_info.inertia_override.value());
    }

    b3Body_SetMassData(m_body, mass_data);
}

Box3d_rigid_body::~Box3d_rigid_body() noexcept
{
    if (!m_is_valid) {
        return;
    }
    // Destroying the body destroys its shapes and any joints attached to it.
    // The derived hulls this body owns are released by m_derived_hulls.
    b3DestroyBody(m_body);
    m_is_valid = false;
}

void Box3d_rigid_body::set_enabled_in_world(const bool enabled)
{
    if (!m_is_valid) {
        return;
    }
    if (enabled) {
        b3Body_Enable(m_body);
    } else {
        b3Body_Disable(m_body);
    }
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------

auto Box3d_rigid_body::get_angular_damping() const -> float
{
    return m_is_valid ? b3Body_GetAngularDamping(m_body) : 0.0f;
}

auto Box3d_rigid_body::get_angular_velocity() const -> glm::vec3
{
    return m_is_valid ? from_box3d(b3Body_GetAngularVelocity(m_body)) : glm::vec3{0.0f};
}

auto Box3d_rigid_body::get_center_of_mass() const -> glm::vec3
{
    return m_is_valid ? from_box3d(b3Body_GetLocalCenter(m_body)) : glm::vec3{0.0f};
}

auto Box3d_rigid_body::get_center_of_mass_transform() const -> Transform
{
    if (!m_is_valid) {
        return Transform{};
    }
    const b3Transform body_transform = b3Body_GetTransform(m_body);
    const glm::quat   rotation       = from_box3d(body_transform.q);
    const glm::vec3   center         = from_box3d(b3Body_GetWorldCenter(m_body));
    return Transform{glm::mat3_cast(rotation), center};
}

auto Box3d_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Box3d_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}

auto Box3d_rigid_body::get_friction() const -> float
{
    // Friction is carried per shape; every shape of a body is created with the
    // same base material, so the first one is representative.
    if (!m_is_valid || m_shape_ids.empty()) {
        return 0.0f;
    }
    return b3Shape_GetFriction(m_shape_ids.front());
}

auto Box3d_rigid_body::get_gravity_factor() const -> float
{
    return m_is_valid ? b3Body_GetGravityScale(m_body) : 1.0f;
}

auto Box3d_rigid_body::get_linear_damping() const -> float
{
    return m_is_valid ? b3Body_GetLinearDamping(m_body) : 0.0f;
}

auto Box3d_rigid_body::get_linear_velocity() const -> glm::vec3
{
    return m_is_valid ? from_box3d(b3Body_GetLinearVelocity(m_body)) : glm::vec3{0.0f};
}

auto Box3d_rigid_body::get_local_inertia() const -> glm::mat4
{
    return m_is_valid ? inertia_from_box3d(b3Body_GetLocalRotationalInertia(m_body)) : glm::mat4{0.0f};
}

auto Box3d_rigid_body::get_mass() const -> float
{
    return m_is_valid ? b3Body_GetMass(m_body) : 0.0f;
}

auto Box3d_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

auto Box3d_rigid_body::get_restitution() const -> float
{
    if (!m_is_valid || m_shape_ids.empty()) {
        return 0.0f;
    }
    return b3Shape_GetRestitution(m_shape_ids.front());
}

auto Box3d_rigid_body::get_world_transform() const -> glm::mat4
{
    if (!m_is_valid) {
        return glm::mat4{1.0f};
    }
    const b3Transform body_transform = b3Body_GetTransform(m_body);
    glm::mat4 result = glm::mat4_cast(from_box3d(body_transform.q));
    result[3] = glm::vec4{from_box3d(body_transform.p), 1.0f};
    return result;
}

auto Box3d_rigid_body::is_active() const -> bool
{
    return m_is_valid && b3Body_IsAwake(m_body);
}

auto Box3d_rigid_body::get_allow_sleeping() const -> bool
{
    return m_allow_sleeping;
}

auto Box3d_rigid_body::get_physics_material() const -> std::shared_ptr<Physics_material>
{
    return m_physics_material;
}

auto Box3d_rigid_body::get_collision_filter() const -> std::shared_ptr<Collision_filter>
{
    return m_collision_filter;
}

auto Box3d_rigid_body::get_owner() const -> void*
{
    return m_owner;
}

// -----------------------------------------------------------------------------
// Mutators
// -----------------------------------------------------------------------------

void Box3d_rigid_body::begin_move()
{
    if (!m_is_valid) {
        return;
    }
    set_allow_sleeping(false);
    b3Body_SetAwake(m_body, true);
}

void Box3d_rigid_body::end_move()
{
    if (!m_is_valid) {
        return;
    }
    set_allow_sleeping(true);
}

void Box3d_rigid_body::set_angular_velocity(const glm::vec3& velocity)
{
    if (m_is_valid) {
        b3Body_SetAngularVelocity(m_body, to_box3d(velocity));
    }
}

void Box3d_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    if (m_is_valid) {
        b3Body_SetLinearDamping (m_body, linear_damping);
        b3Body_SetAngularDamping(m_body, angular_damping);
    }
}

void Box3d_rigid_body::set_friction(const float friction)
{
    for (const b3ShapeId shape_id : m_shape_ids) {
        b3Shape_SetFriction(shape_id, friction);
    }
}

void Box3d_rigid_body::set_gravity_factor(const float gravity_factor)
{
    if (m_is_valid) {
        b3Body_SetGravityScale(m_body, gravity_factor);
    }
}

void Box3d_rigid_body::set_linear_velocity(const glm::vec3& velocity)
{
    if (m_is_valid) {
        b3Body_SetLinearVelocity(m_body, to_box3d(velocity));
    }
}

void Box3d_rigid_body::set_mass_properties(const float mass, const glm::mat4& local_inertia)
{
    if (!m_is_valid) {
        return;
    }
    b3MassData mass_data = b3Body_GetMassData(m_body);
    mass_data.mass    = mass;
    mass_data.inertia = inertia_to_box3d(local_inertia);
    b3Body_SetMassData(m_body, mass_data);
}

void Box3d_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    if (!m_is_valid || (m_motion_mode == motion_mode)) {
        return;
    }
    m_motion_mode = motion_mode;
    b3Body_SetType(m_body, to_box3d_body_type(motion_mode));
    if (motion_mode == Motion_mode::e_dynamic) {
        b3Body_SetAwake(m_body, true);
    }
}

void Box3d_rigid_body::set_restitution(const float restitution)
{
    for (const b3ShapeId shape_id : m_shape_ids) {
        b3Shape_SetRestitution(shape_id, restitution);
    }
}

void Box3d_rigid_body::set_world_transform(const Transform& transform)
{
    if (!m_is_valid) {
        return;
    }
    const b3Transform target = to_box3d(transform);
    if (m_motion_mode == Motion_mode::e_kinematic_physical) {
        // Turns the pose delta into a velocity so the body pushes what it hits,
        // which is what interactive dragging wants. This is Box3D's equivalent
        // of Jolt's MoveKinematic.
        b3Body_SetTargetTransform(m_body, target, kinematic_target_time_step, true);
        return;
    }
    b3Body_SetTransform(m_body, target.p, target.q);
}

void Box3d_rigid_body::teleport(const Transform& transform)
{
    if (!m_is_valid) {
        return;
    }
    // Motion-mode independent instantaneous placement: never induces a
    // velocity, unlike the kinematic-physical path in set_world_transform().
    const b3Transform target = to_box3d(transform);
    b3Body_SetTransform(m_body, target.p, target.q);
}

void Box3d_rigid_body::set_allow_sleeping(const bool value)
{
    if (!m_is_valid) {
        return;
    }
    m_allow_sleeping = value;
    b3Body_EnableSleep(m_body, value);
}

void Box3d_rigid_body::set_owner(void* owner)
{
    m_owner = owner;
}

void Box3d_rigid_body::set_physics_material(const std::shared_ptr<Physics_material>& material)
{
    m_physics_material = material;
}

void Box3d_rigid_body::set_collision_filter(const std::shared_ptr<Collision_filter>& filter)
{
    m_collision_filter = filter;
}

} // namespace erhe::physics
