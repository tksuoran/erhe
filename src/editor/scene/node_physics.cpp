#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_material.hpp"
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
using erhe::property::Dependency_object;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

namespace {

constexpr std::string_view c_group = "Rigid Body";
const erhe::property::Owner_type c_owner = Node_physics::property_owner_type();
using Material_traits = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Physics_material>>;
using Filter_traits   = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Collision_filter>>;

// Evaluated on Node_physics objects only (a holder of Node_physics values
// lists them by its own value, doc/property-system.md D30).
auto is_movable(const Dependency_object& object) -> bool
{
    return static_cast<const Node_physics&>(object).get_motion_mode() != Motion_mode::e_static;
}

auto slider(const float min, const float max, const std::string_view label, const std::string_view tooltip = {}, const Property_ui::Visible_when visible_when = {}) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .group = c_group, .tooltip = tooltip, .label = label, .visible_when = visible_when};
}

} // anonymous namespace

// Every property is entry-stored and inherits (doc/property-system.md
// section 4.10, D30): a node above or a style holds Node_physics.* for
// the bodies below it. The create info and m_motion_mode mirror the
// effective values (on_property_changed), so the body is (re)created from
// a plain struct.
const Property<Motion_mode> Node_physics::motion_mode_property = Property<Motion_mode>::register_property(
    "motion_mode", c_owner, erhe::physics::c_motion_mode_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Motion_mode::e_dynamic),
        .inherits      = true,
        .ui            = Property_ui{.group = c_group, .tooltip = "The intended mode; a static trigger body is created kinematic non-physical", .label = "Motion Mode"}
    }
);
const Property<bool> Node_physics::is_trigger_property = Property<bool>::register_property(
    "is_trigger", c_owner,
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_group, .tooltip = "Sensor body: reports overlaps, no collision response (recreates the rigid body)", .label = "Is Trigger"}}
);
const Property<float> Node_physics::mass_property = Property<float>::register_property(
    "mass", c_owner,
    Property_metadata{
        .default_value = 1.0f,
        .inherits      = true,
        .ui            = Property_ui{.min = 0.01f, .max = 1000.0f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_group, .tooltip = "kg; while no value is set (source default) the body's mass is its shape mass scaled by the material density", .label = "Mass"}
    },
    [](const Property_value& value) -> bool { return std::get<float>(value) > 0.0f; }
);
const Property<float> Node_physics::gravity_factor_property = Property<float>::register_property(
    "gravity_factor", c_owner,
    Property_metadata{.default_value = 1.0f, .inherits = true, .ui = slider(0.0f, 2.0f, "Gravity Factor", {}, is_movable)}
);
const Property<glm::vec3> Node_physics::initial_linear_velocity_property = Property<glm::vec3>::register_property(
    "initial_linear_velocity", c_owner,
    Property_metadata{.default_value = glm::vec3{0.0f}, .inherits = true, .ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "World space; applied when the rigid body is (re)created", .label = "Initial Linear Velocity"}}
);
const Property<glm::vec3> Node_physics::initial_angular_velocity_property = Property<glm::vec3>::register_property(
    "initial_angular_velocity", c_owner,
    Property_metadata{.default_value = glm::vec3{0.0f}, .inherits = true, .ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "World space; applied when the rigid body is (re)created", .label = "Initial Angular Velocity"}}
);
const Property<glm::vec3> Node_physics::center_of_mass_offset_property = Property<glm::vec3>::register_property(
    "center_of_mass_offset", c_owner,
    Property_metadata{.default_value = glm::vec3{0.0f}, .inherits = true, .ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Offset-center-of-mass wrapper around the collision shape (recreates the rigid body)", .label = "Center of Mass"}}
);
const Property<Object_reference> Node_physics::physics_material_property = Property<Object_reference>::register_property(
    "physics_material", c_owner,
    Property_metadata{.inherits = true, .ui = Property_ui{.group = c_group, .tooltip = "Shared material carrying friction, restitution, damping, wind receptivity and density; none behaves like the material defaults", .label = "Physics Material", .reference_item_types = erhe::Item_type::physics_material}},
    Material_traits::validate
);
const Property<Object_reference> Node_physics::collision_filter_property = Property<Object_reference>::register_property(
    "collision_filter", c_owner,
    Property_metadata{.inherits = true, .ui = Property_ui{.group = c_group, .label = "Collision Filter", .reference_item_types = erhe::Item_type::collision_filter}},
    Filter_traits::validate
);

Node_physics::Node_physics(const Node_physics& src)
    : Item              {src} // the property entries copy with the base (D10)
    , markers           {src.markers}
    , m_physics_world   {src.m_physics_world}
    , m_create_info     {src.m_create_info}
    , m_rigid_body      {src.m_rigid_body}
    , m_motion_mode     {src.m_motion_mode}
    , m_wake_on_attach  {src.m_wake_on_attach}
{
    observe_physics_material();
}

Node_physics& Node_physics::operator=(const Node_physics& src)
{
    Item::operator=(src);
    markers            = src.markers;
    m_physics_world    = src.m_physics_world;
    m_create_info      = src.m_create_info;
    m_rigid_body       = src.m_rigid_body;
    m_motion_mode      = src.m_motion_mode;
    m_wake_on_attach   = src.m_wake_on_attach;
    observe_physics_material();
    return *this;
}

Node_physics::Node_physics(const Node_physics& src, erhe::for_clone)
    : Item          {src, erhe::for_clone{}} // the property entries copy with the base (D10)
    , markers       {}        // clone does not initially have markers
    , m_physics_world{nullptr} // clone is initially detached
    , m_create_info {src.m_create_info}
    , m_rigid_body  {}        // clone rigid body is not initially created
    , m_motion_mode {src.m_motion_mode}
{
    observe_physics_material();
}

Node_physics::Node_physics(const IRigid_body_create_info& create_info)
    : m_create_info{create_info}
    , m_motion_mode{create_info.motion_mode}
{
    // The create info's fields become local values where they differ from
    // the property defaults; a field at its default stays unset, so a node
    // above or a style can hold it (a set_value here reaches
    // on_property_changed, whose consequences are no-ops while detached).
    if (create_info.motion_mode != Motion_mode::e_dynamic)  { set_value(motion_mode_property, create_info.motion_mode); }
    if (create_info.is_sensor)                              { set_value(is_trigger_property, true); }
    if (create_info.mass.has_value())                       { set_value(mass_property, create_info.mass.value()); }
    if (create_info.gravity_factor != 1.0f)                 { set_value(gravity_factor_property, create_info.gravity_factor); }
    if (create_info.linear_velocity != glm::vec3{0.0f})     { set_value(initial_linear_velocity_property, create_info.linear_velocity); }
    if (create_info.angular_velocity != glm::vec3{0.0f})    { set_value(initial_angular_velocity_property, create_info.angular_velocity); }
    const glm::vec3 offset = get_center_of_mass_offset();
    if (offset != glm::vec3{0.0f})                          { set_value(center_of_mass_offset_property, offset); }
    if (create_info.physics_material)                       { set_value(physics_material_property, Material_traits::to_value(create_info.physics_material)); }
    if (create_info.collision_filter)                       { set_value(collision_filter_property, Filter_traits::to_value(create_info.collision_filter)); }
    observe_physics_material();
}

void Node_physics::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (!erhe::property::is_owner_type_or_descendant(c_owner, args.property.get_owner_type())) {
        return;
    }
    const erhe::property::Dependency_property* const changed = &args.property;
    if (changed == motion_mode_property.get_ptr()) {
        m_motion_mode = get_value(motion_mode_property);
        apply_motion_mode();
    } else if (changed == is_trigger_property.get_ptr()) {
        m_create_info.is_sensor = get_value(is_trigger_property);
        recreate_rigid_body();
    } else if (changed == mass_property.get_ptr()) {
        if (get_value_source(mass_property) == erhe::property::Value_source::default_value) {
            // No mass anywhere: back to the shape mass scaled by the
            // material density, which only a (re)creation computes.
            if (m_create_info.mass.has_value()) {
                m_create_info.mass.reset();
                recreate_rigid_body();
            }
        } else {
            apply_mass(get_value(mass_property));
        }
    } else if (changed == gravity_factor_property.get_ptr()) {
        m_create_info.gravity_factor = get_value(gravity_factor_property);
        apply_gravity_factor();
    } else if (changed == initial_linear_velocity_property.get_ptr()) {
        m_create_info.linear_velocity = get_value(initial_linear_velocity_property);
    } else if (changed == initial_angular_velocity_property.get_ptr()) {
        m_create_info.angular_velocity = get_value(initial_angular_velocity_property);
    } else if (changed == center_of_mass_offset_property.get_ptr()) {
        apply_center_of_mass_offset(get_value(center_of_mass_offset_property));
    } else if (changed == physics_material_property.get_ptr()) {
        m_create_info.physics_material = Material_traits::from_value(get_value(physics_material_property));
        observe_physics_material();
        reapply_physics_material();
    } else if (changed == collision_filter_property.get_ptr()) {
        m_create_info.collision_filter = Filter_traits::from_value(get_value(collision_filter_property));
        reapply_collision_filter();
    }
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
    // An inactive item is out of the simulation (X2): no body is made until
    // the derived Item_flags::active bit comes back, which
    // handle_flag_bits_update() acts on.
    if ((new_item_host != nullptr) && is_active()) {
        Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
        auto& physics_world = new_scene_root->get_physics_world();

        log_physics->trace("making rigid body for {}", get_node()->get_name());
        create_rigid_body(physics_world);
        new_scene_root->register_node_physics(shared_this);
        if (m_wake_on_attach && m_rigid_body && (m_rigid_body->get_motion_mode() == Motion_mode::e_dynamic)) {
            m_rigid_body->begin_move();
            m_rigid_body->end_move();
        }
    }
}

void Node_physics::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    erhe::scene::Node_attachment::handle_flag_bits_update(old_flag_bits, new_flag_bits);
    if (((old_flag_bits ^ new_flag_bits) & erhe::Item_flags::active) == 0u) {
        return;
    }
    erhe::Item_host* const item_host = get_item_host();
    if (item_host == nullptr) {
        return;
    }
    Scene_root* const                   scene_root  = static_cast<Scene_root*>(item_host);
    const std::shared_ptr<Node_physics> shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());
    if (is_active()) {
        create_rigid_body(scene_root->get_physics_world());
        scene_root->register_node_physics(shared_this);
    } else {
        scene_root->unregister_node_physics(shared_this);
        m_rigid_body.reset();
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
    // The effective center-of-mass offset stays what the property says:
    // the new shape gets the wrapper the old one carried.
    const glm::vec3 offset = get_value(center_of_mass_offset_property);
    m_create_info.collision_shape = ((offset != glm::vec3{0.0f}) && collision_shape)
        ? erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(collision_shape, offset)
        : collision_shape;
    recreate_rigid_body();
}

auto Node_physics::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Node_physics::set_motion_mode(Motion_mode mode)
{
    set_value(motion_mode_property, mode);
}

void Node_physics::apply_motion_mode()
{
    m_create_info.motion_mode = get_effective_motion_mode();
    if (m_rigid_body) {
        m_rigid_body->set_motion_mode(m_create_info.motion_mode);
    }
}

auto Node_physics::get_mass() const -> float
{
    if (m_create_info.mass.has_value()) {
        return m_create_info.mass.value();
    }
    const IRigid_body* rigid_body = get_rigid_body();
    return (rigid_body != nullptr) ? rigid_body->get_mass() : 0.0f;
}

void Node_physics::set_mass(const float mass)
{
    set_value(mass_property, mass);
}

void Node_physics::apply_mass(const float mass)
{
    if (m_create_info.mass.has_value() && (mass == m_create_info.mass.value())) {
        return;
    }
    m_create_info.mass = mass;
    IRigid_body* rigid_body = get_rigid_body();
    if (rigid_body != nullptr) {
        // Inertia scales linearly with mass for a fixed shape (the same
        // ratio math as the create-path mass override in brush.cpp);
        // keeping the old inertia would leave the body tumbling as if it
        // still had the old mass.
        const float old_mass = rigid_body->get_mass();
        glm::mat4 inertia = rigid_body->get_local_inertia();
        if (old_mass > 0.0f) {
            inertia = glm::mat4{glm::mat3{inertia} * (mass / old_mass)};
        }
        rigid_body->set_mass_properties(mass, inertia);
    }
}

auto Node_physics::get_physics_material() const -> const std::shared_ptr<erhe::physics::Physics_material>&
{
    return m_create_info.physics_material;
}

void Node_physics::set_physics_material(const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    set_value(physics_material_property, Material_traits::to_value(physics_material));
}

void Node_physics::reapply_physics_material()
{
    if (m_rigid_body) {
        m_rigid_body->set_physics_material(m_create_info.physics_material);
    }
}

void Node_physics::observe_physics_material()
{
    m_physics_material_observer.release();
    if (m_create_info.physics_material) {
        m_physics_material_observer = m_create_info.physics_material->add_observer(
            [this](erhe::property::Dependency_object&, const erhe::property::Property_changed_args&) { reapply_physics_material(); }
        );
    }
}

auto Node_physics::get_collision_filter() const -> const std::shared_ptr<erhe::physics::Collision_filter>&
{
    return m_create_info.collision_filter;
}

void Node_physics::set_collision_filter(const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    set_value(collision_filter_property, Filter_traits::to_value(collision_filter));
}

void Node_physics::reapply_collision_filter()
{
    if (m_rigid_body) {
        m_rigid_body->set_collision_filter(m_create_info.collision_filter);
    }
}

auto Node_physics::is_trigger() const -> bool
{
    return m_create_info.is_sensor;
}

void Node_physics::set_trigger(const bool trigger)
{
    set_value(is_trigger_property, trigger);
}

auto Node_physics::get_gravity_factor() const -> float
{
    return m_create_info.gravity_factor;
}

void Node_physics::set_gravity_factor(const float gravity_factor)
{
    set_value(gravity_factor_property, gravity_factor);
}

void Node_physics::apply_gravity_factor()
{
    if (m_rigid_body && (m_rigid_body->get_motion_mode() != Motion_mode::e_static)) {
        m_rigid_body->set_gravity_factor(m_create_info.gravity_factor);
    }
}

void Node_physics::set_wake_on_attach(const bool wake_on_attach)
{
    m_wake_on_attach = wake_on_attach;
}

auto Node_physics::get_initial_linear_velocity() const -> glm::vec3
{
    return m_create_info.linear_velocity;
}

void Node_physics::set_initial_linear_velocity(const glm::vec3& velocity)
{
    set_value(initial_linear_velocity_property, velocity);
}

auto Node_physics::get_initial_angular_velocity() const -> glm::vec3
{
    return m_create_info.angular_velocity;
}

void Node_physics::set_initial_angular_velocity(const glm::vec3& velocity)
{
    set_value(initial_angular_velocity_property, velocity);
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
    set_value(center_of_mass_offset_property, offset);
}

void Node_physics::apply_center_of_mass_offset(const glm::vec3& offset)
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
