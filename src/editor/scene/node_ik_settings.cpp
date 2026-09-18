#include "scene/node_ik_settings.hpp"

#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"

#include <glm/gtc/constants.hpp>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using Node_traits = erhe::property::Member_value_traits<std::shared_ptr<erhe::scene::Node>>;

constexpr std::string_view c_ik_group = "IK";

[[nodiscard]] auto clamp_vec3(const Property_value& value, const float min_value, const float max_value) -> Property_value
{
    return glm::clamp(std::get<glm::vec3>(value), glm::vec3{min_value}, glm::vec3{max_value});
}

constexpr const char* c_lock_tooltip =
    "IK DOF lock: a locked axis does not rotate under IK. Locks win over limits on the same axis. "
    "A lock on the bone's twist axis has no effect (IK never generates twist).";
constexpr const char* c_limit_tooltip =
    "Enable the rotation limit about this local axis, relative to the rest orientation (range from Limit Min / Limit Max)";

} // anonymous namespace

// Section 4.19: entry-stored; every property but rest_rotation inherits
// from the node chain (D30). The members are a mirror refreshed by
// Ik_settings::on_property_changed.
const Property<bool> Ik_settings::lock_x_property = Property<bool>::register_property(
    "lock_x", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock X"}}
);
const Property<bool> Ik_settings::lock_y_property = Property<bool>::register_property(
    "lock_y", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock Y"}}
);
const Property<bool> Ik_settings::lock_z_property = Property<bool>::register_property(
    "lock_z", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock Z"}}
);
const Property<bool> Ik_settings::limit_x_property = Property<bool>::register_property(
    "limit_x", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit X"}}
);
const Property<bool> Ik_settings::limit_y_property = Property<bool>::register_property(
    "limit_y", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit Y"}}
);
const Property<bool> Ik_settings::limit_z_property = Property<bool>::register_property(
    "limit_z", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = false, .inherits = true, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit Z"}}
);
const Property<glm::vec3> Ik_settings::limit_min_property = Property<glm::vec3>::register_property(
    "limit_min", Ik_settings::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{-glm::pi<float>()},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, -glm::pi<float>(), 0.0f); },
        .inherits      = true,
        .ui            = Property_ui{.min = -glm::pi<float>(), .max = 0.0f, .presentation = Property_ui::Presentation::angle_degrees, .group = c_ik_group, .tooltip = "Per-axis lower rotation limit relative to the rest orientation, in [-180, 0] degrees", .label = "Limit Min"}
    }
);
const Property<glm::vec3> Ik_settings::limit_max_property = Property<glm::vec3>::register_property(
    "limit_max", Ik_settings::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{glm::pi<float>()},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, 0.0f, glm::pi<float>()); },
        .inherits      = true,
        .ui            = Property_ui{.min = 0.0f, .max = glm::pi<float>(), .presentation = Property_ui::Presentation::angle_degrees, .group = c_ik_group, .tooltip = "Per-axis upper rotation limit relative to the rest orientation, in [0, 180] degrees", .label = "Limit Max"}
    }
);
const Property<glm::vec3> Ik_settings::stiffness_property = Property<glm::vec3>::register_property(
    "stiffness", Ik_settings::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{0.0f},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, 0.0f, 0.99f); },
        .inherits      = true,
        .ui            = Property_ui{.min = 0.0f, .max = 0.99f, .step = 0.01f, .group = c_ik_group, .tooltip = "Per-axis resistance to rotation (0..0.99); serialized, not yet enforced by the solver", .developer_only = true, .label = "Stiffness"}
    }
);
const Property<glm::quat> Ik_settings::rest_rotation_property = Property<glm::quat>::register_property(
    "rest_rotation", Ik_settings::property_owner_type(),
    Property_metadata{.default_value = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, .ui = Property_ui{.group = c_ik_group, .tooltip = "Reference orientation that defines the zero angle of the limits (parent-from-bone rotation)", .label = "Rest Rotation"}}
);

// R1: the pole node bridged over the weak member (no strong node-to-node
// reference), the angle in the entry store, inheriting. The angle is periodic,
// so it is not coerced - a value outside the drag range is legal and names the
// same pose (R17).
const Property<Object_reference> Ik_settings::pole_target_property = Property<Object_reference>::register_property(
    "pole_target", Ik_settings::property_owner_type(),
    Property_metadata{
        .ui = Property_ui{
            .group                = c_ik_group,
            .tooltip              = "Node the chain's bend plane is aimed at. The governing pole is the one authored nearest the effector; a pole in another scene, an inactive one, or one that is itself a chain joint is ignored for the drag.",
            .label                = "Pole Target",
            .reference_item_types = erhe::Item_type::xformable
        },
        .bridge = erhe::property::Property_bridge{
            .get = [](const Dependency_object& object) -> Property_value {
                return Node_traits::to_value(static_cast<const Ik_settings&>(object).get_pole_target());
            },
            .set = [](Dependency_object& object, const Property_value& value) {
                static_cast<Ik_settings&>(object).set_pole_target(Node_traits::from_value(value));
            }
        }
    },
    Node_traits::validate
);
const Property<float> Ik_settings::pole_angle_property = Property<float>::register_property(
    "pole_angle", Ik_settings::property_owner_type(),
    Property_metadata{
        .default_value = 0.0f,
        .inherits      = true,
        .ui            = Property_ui{
            .min          = -glm::pi<float>(),
            .max          =  glm::pi<float>(),
            .presentation = Property_ui::Presentation::angle_degrees,
            .group        = c_ik_group,
            .tooltip      = "Swivel offset of the bend about the root-to-effector line, in degrees, right-handed about the direction from root to effector",
            .label        = "Pole Angle"
        }
    }
);

auto Ik_settings::lock_property(const int axis) -> const Property<bool>&
{
    switch (axis) {
        case 0:  return lock_x_property;
        case 1:  return lock_y_property;
        default: return lock_z_property;
    }
}

auto Ik_settings::limit_property(const int axis) -> const Property<bool>&
{
    switch (axis) {
        case 0:  return limit_x_property;
        case 1:  return limit_y_property;
        default: return limit_z_property;
    }
}

Ik_settings::Ik_settings(const Ik_settings&) = default;
Ik_settings::~Ik_settings() noexcept         = default;

Ik_settings::Ik_settings(const std::string_view name)
    : Item{name}
{
}

Ik_settings::Ik_settings(const Ik_settings& src, erhe::for_clone)
    : Item        {src, erhe::for_clone{}}
    , m_data      {src.m_data}
    , m_pole_target{src.m_pole_target} // R2: a clone keeps naming the same pole node
{
}

void Ik_settings::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (erhe::property::is_owner_type_or_descendant(Ik_settings::property_owner_type(), args.property.get_owner_type())) {
        refresh_mirror();
    }
}

void Ik_settings::refresh_mirror()
{
    for (int axis = 0; axis < 3; ++axis) {
        m_data.lock [axis] = get_value(lock_property(axis));
        m_data.limit[axis] = get_value(limit_property(axis));
    }
    m_data.limit_min     = get_value(limit_min_property);
    m_data.limit_max     = get_value(limit_max_property);
    m_data.stiffness     = get_value(stiffness_property);
    m_data.rest_rotation = get_value(rest_rotation_property);
    m_data.pole_angle    = get_value(pole_angle_property);
}

auto Ik_settings::get_pole_target() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_pole_target.lock();
}

void Ik_settings::set_pole_target(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_pole_target.lock() == node) {
        return;
    }
    m_pole_target = node;
    invalidate_dependents(pole_target_property.get()); // bridged storage changed outside set_value (D22)
}

void Ik_settings::set_lock         (const int axis, const bool value)  { set_value(lock_property(axis), value); }
void Ik_settings::set_limit        (const int axis, const bool value)  { set_value(limit_property(axis), value); }
void Ik_settings::set_limit_min    (const glm::vec3& value)            { set_value(limit_min_property, value); }
void Ik_settings::set_limit_max    (const glm::vec3& value)            { set_value(limit_max_property, value); }
void Ik_settings::set_stiffness    (const glm::vec3& value)            { set_value(stiffness_property, value); }
void Ik_settings::set_rest_rotation(const glm::quat& value)            { set_value(rest_rotation_property, value); }
void Ik_settings::set_pole_angle   (const float value)                 { set_value(pole_angle_property, value); }

} // namespace editor
