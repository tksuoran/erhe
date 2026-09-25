#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

// Effective IK values of one bone node, as the IK solver reads them
// (doc/plans/rigging/ik_settings.md section 1: read with read_ik_settings()).
//
// Parameters are per local axis x = 0, y = 1, z = 2. lock wins over limit
// on the same axis. Limits are radians with min in [-pi, 0] and max in
// [0, pi], so the rest angle 0 is always legal. stiffness (0..0.99) scales
// the joint's per-iteration change in the constrained solve (section 4).
class Ik_settings_data
{
public:
    std::array<bool, 3> lock     {false, false, false};
    std::array<bool, 3> limit    {false, false, false};
    glm::vec3           limit_min{-glm::pi<float>(), -glm::pi<float>(), -glm::pi<float>()};
    glm::vec3           limit_max{ glm::pi<float>(),  glm::pi<float>(),  glm::pi<float>()};
    glm::vec3           stiffness{0.0f, 0.0f, 0.0f};

    // Pole swivel offset about the chain's root-to-effector line, radians
    // (doc/plans/rigging/pole_target.md R3). The pole node itself is not in
    // this record - it is a reference, read with get_ik_pole_target().
    float               pole_angle{0.0f};

    // Reference orientation defining the zero of the limits: the limited
    // quantity is inverse(rest_rotation) * parent_from_node_rotation. Its
    // per-object default is the node's bind-pose local rotation
    // (doc/plans/rigging/ik_settings.md section 1).
    glm::quat           rest_rotation{1.0f, 0.0f, 0.0f, 0.0f};

    auto operator==(const Ik_settings_data&) const -> bool = default;
};

// Per-bone IK settings as attached properties of the bone node itself
// (doc/plans/rigging/ik_settings.md section 1): per-axis DOF locks and joint
// rotation limits enforced by the constrained IK solver via swing/twist
// decomposition relative to rest_rotation, plus the pole target and angle.
//
// Ik is a registration holder, not an item and not a Dependency_object: it
// owns the property registrations (owner type "Ik", so the qualified names
// are Ik.lock_x .. Ik.pole_angle) and the holder of every value is an
// erhe::scene::Node. A bone without any local value is unconstrained.
class Ik
{
public:
    Ik() = delete;

    // The registering class's owner type id. Ik has no instances, so it is
    // allocated directly under the root rather than by Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // Attached to erhe::scene::Node, UI group "IK", none of them inheriting
    // (a shared limit set is a Style holding the Ik.* values). limit_min
    // / limit_max are radians shown in degrees, coerced per component to
    // [-pi, 0] and [0, pi]; stiffness is coerced to [0, 0.99].
    static const erhe::property::Property<bool>      lock_x_property;
    static const erhe::property::Property<bool>      lock_y_property;
    static const erhe::property::Property<bool>      lock_z_property;
    static const erhe::property::Property<bool>      limit_x_property;
    static const erhe::property::Property<bool>      limit_y_property;
    static const erhe::property::Property<bool>      limit_z_property;
    static const erhe::property::Property<glm::vec3> limit_min_property;
    static const erhe::property::Property<glm::vec3> limit_max_property;
    static const erhe::property::Property<glm::vec3> stiffness_property;
    // Per-object default from the bind pose, identity when the node is not
    // a joint whose parent is a joint of the same skin.
    static const erhe::property::Property<glm::quat> rest_rotation_property;
    // A weak object reference, so a pole is never an ownership edge.
    // Any node is accepted, including the bone itself and nodes of
    // other scenes; admissibility is decided once per drag by
    // Ik_drag::discover_pole (doc/plans/rigging/pole_target.md R4, R8).
    static const erhe::property::Property<erhe::property::Weak_object_reference> pole_target_property;
    static const erhe::property::Property<float>    pole_angle_property;

    [[nodiscard]] static auto lock_property (int axis) -> const erhe::property::Property<bool>&;
    [[nodiscard]] static auto limit_property(int axis) -> const erhe::property::Property<bool>&;

    // Every Ik.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// The effective Ik.* values of one node, read once per chain joint at
// drag begin.
[[nodiscard]] auto read_ik_settings(const erhe::scene::Node& node) -> Ik_settings_data;

// The node Ik.pole_target names, or null when nothing is authored or the
// target has expired.
[[nodiscard]] auto get_ik_pole_target(const erhe::scene::Node& node) -> std::shared_ptr<erhe::scene::Node>;
void set_ik_pole_target(erhe::scene::Node& node, const std::shared_ptr<erhe::scene::Node>& pole);

// True when the node holds a local value of any Ik.* property - what a
// writer that has no form for IK data counts
// (doc/plans/rigging/ik_settings.md section 6).
[[nodiscard]] auto has_local_ik_value(const erhe::scene::Node& node) -> bool;

} // namespace editor
