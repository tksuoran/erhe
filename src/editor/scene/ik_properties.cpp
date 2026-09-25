#include "scene/ik_properties.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"

#include <glm/gtc/constants.hpp>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using erhe::property::Weak_object_reference;
using Pole_traits = erhe::property::Member_value_traits<std::weak_ptr<erhe::scene::Node>>;

constexpr std::string_view c_ik_group = "IK";

[[nodiscard]] auto clamp_vec3(const Property_value& value, const float min_value, const float max_value) -> Property_value
{
    return glm::clamp(std::get<glm::vec3>(value), glm::vec3{min_value}, glm::vec3{max_value});
}

// P4: the rows are offered on bone nodes. The D12 listing rule still lists a
// row on any object holding a local value, so a node that lost its bone flag
// keeps showing what it holds.
[[nodiscard]] auto is_bone_node(const Dependency_object& object) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    return (node != nullptr) && erhe::scene::is_bone(node);
}

constexpr const char* c_lock_tooltip =
    "IK DOF lock: a locked axis does not rotate under IK. Locks win over limits on the same axis. "
    "Two locked axes leave a hinge about the third.";
constexpr const char* c_limit_tooltip =
    "Enable the rotation limit about this local axis, relative to the rest orientation (range from Limit Min / Limit Max)";

} // anonymous namespace

auto Ik::property_owner_type() -> erhe::property::Owner_type
{
    // Ik is not a Dependency_object, so there is no Item<> to allocate the id:
    // the registering class's own id sits directly under the root and serves
    // only to qualify the names (Ik.lock_x) and to name the owner in logs.
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Ik"
    );
    return s_id;
}

const Property<bool> Ik::lock_x_property = Property<bool>::register_attached(
    "lock_x", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock X", .visible_when = is_bone_node}}
);
const Property<bool> Ik::lock_y_property = Property<bool>::register_attached(
    "lock_y", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock Y", .visible_when = is_bone_node}}
);
const Property<bool> Ik::lock_z_property = Property<bool>::register_attached(
    "lock_z", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_lock_tooltip, .label = "Lock Z", .visible_when = is_bone_node}}
);
const Property<bool> Ik::limit_x_property = Property<bool>::register_attached(
    "limit_x", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit X", .visible_when = is_bone_node}}
);
const Property<bool> Ik::limit_y_property = Property<bool>::register_attached(
    "limit_y", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit Y", .visible_when = is_bone_node}}
);
const Property<bool> Ik::limit_z_property = Property<bool>::register_attached(
    "limit_z", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_ik_group, .tooltip = c_limit_tooltip, .label = "Limit Z", .visible_when = is_bone_node}}
);
const Property<glm::vec3> Ik::limit_min_property = Property<glm::vec3>::register_attached(
    "limit_min", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{-glm::pi<float>()},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, -glm::pi<float>(), 0.0f); },
        .ui            = Property_ui{.min = -glm::pi<float>(), .max = 0.0f, .presentation = Property_ui::Presentation::angle_degrees, .group = c_ik_group, .tooltip = "Per-axis lower rotation limit relative to the rest orientation, in [-180, 0] degrees", .label = "Limit Min", .visible_when = is_bone_node}
    }
);
const Property<glm::vec3> Ik::limit_max_property = Property<glm::vec3>::register_attached(
    "limit_max", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{glm::pi<float>()},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, 0.0f, glm::pi<float>()); },
        .ui            = Property_ui{.min = 0.0f, .max = glm::pi<float>(), .presentation = Property_ui::Presentation::angle_degrees, .group = c_ik_group, .tooltip = "Per-axis upper rotation limit relative to the rest orientation, in [0, 180] degrees", .label = "Limit Max", .visible_when = is_bone_node}
    }
);
const Property<glm::vec3> Ik::stiffness_property = Property<glm::vec3>::register_attached(
    "stiffness", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{0.0f},
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value { return clamp_vec3(value, 0.0f, 0.99f); },
        .ui            = Property_ui{.min = 0.0f, .max = 0.99f, .step = 0.01f, .group = c_ik_group, .tooltip = "Per-axis resistance to rotation under IK (0 = free, 0.99 = stiffest): the solve prefers the less stiff joints", .label = "Stiffness", .visible_when = is_bone_node}
    }
);
const Property<glm::quat> Ik::rest_rotation_property = Property<glm::quat>::register_attached(
    "rest_rotation", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value   = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        .ui              = Property_ui{.group = c_ik_group, .tooltip = "Reference orientation that defines the zero angle of the limits (parent-from-bone rotation); unset, the bone's rest rotation (Rig Rest Rotation)", .label = "Rest Rotation", .visible_when = is_bone_node},
        // P5: the rotation of the bone's rest transform
        // (doc/plans/rigging/skeleton_editing.md R2), so the limits frame and
        // the rest pose are one thing; a change of Rig.rest_rotation notifies
        // this property where it follows its default (D31 default_from).
        .default_from    = Rig::rest_rotation_property().get_ptr()
    }
);
// P1: the weak reference kind, so a pole is never a strong node-to-node edge.
const Property<Weak_object_reference> Ik::pole_target_property = Property<Weak_object_reference>::register_attached(
    "pole_target", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .ui = Property_ui{
            .group                = c_ik_group,
            .tooltip              = "Node the chain's bend plane is aimed at. The governing pole is the one authored nearest the effector; a pole in another scene, an inactive one, or one that is itself a chain joint is ignored for the drag.",
            .label                = "Pole Target",
            .visible_when         = is_bone_node,
            .reference_item_types = erhe::Item_type::xformable
        }
    },
    Pole_traits::validate
);
// The angle is periodic, so it is not coerced - a value outside the drag
// range is legal and names the same pose (R17).
const Property<float> Ik::pole_angle_property = Property<float>::register_attached(
    "pole_angle", Ik::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = 0.0f,
        .ui            = Property_ui{
            .min          = -glm::pi<float>(),
            .max          =  glm::pi<float>(),
            .presentation = Property_ui::Presentation::angle_degrees,
            .group        = c_ik_group,
            .tooltip      = "Swivel offset of the bend about the root-to-effector line, in degrees, right-handed about the direction from root to effector",
            .label        = "Pole Angle",
            .visible_when = is_bone_node
        }
    }
);

auto Ik::lock_property(const int axis) -> const Property<bool>&
{
    switch (axis) {
        case 0:  return lock_x_property;
        case 1:  return lock_y_property;
        default: return lock_z_property;
    }
}

auto Ik::limit_property(const int axis) -> const Property<bool>&
{
    switch (axis) {
        case 0:  return limit_x_property;
        case 1:  return limit_y_property;
        default: return limit_z_property;
    }
}

auto Ik::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        lock_x_property       .get_ptr(),
        lock_y_property       .get_ptr(),
        lock_z_property       .get_ptr(),
        limit_x_property      .get_ptr(),
        limit_y_property      .get_ptr(),
        limit_z_property      .get_ptr(),
        limit_min_property    .get_ptr(),
        limit_max_property    .get_ptr(),
        stiffness_property    .get_ptr(),
        rest_rotation_property.get_ptr(),
        pole_target_property  .get_ptr(),
        pole_angle_property   .get_ptr()
    };
    return s_properties;
}

auto read_ik_settings(const erhe::scene::Node& node) -> Ik_settings_data
{
    Ik_settings_data data;
    for (int axis = 0; axis < 3; ++axis) {
        data.lock [static_cast<std::size_t>(axis)] = node.get_value(Ik::lock_property (axis));
        data.limit[static_cast<std::size_t>(axis)] = node.get_value(Ik::limit_property(axis));
    }
    data.limit_min     = node.get_value(Ik::limit_min_property);
    data.limit_max     = node.get_value(Ik::limit_max_property);
    data.stiffness     = node.get_value(Ik::stiffness_property);
    data.rest_rotation = node.get_value(Ik::rest_rotation_property);
    data.pole_angle    = node.get_value(Ik::pole_angle_property);
    return data;
}

auto get_ik_pole_target(const erhe::scene::Node& node) -> std::shared_ptr<erhe::scene::Node>
{
    return Pole_traits::from_value(node.get_value(Ik::pole_target_property.get())).lock();
}

void set_ik_pole_target(erhe::scene::Node& node, const std::shared_ptr<erhe::scene::Node>& pole)
{
    node.set_value(Ik::pole_target_property, Weak_object_reference{pole});
}

auto has_local_ik_value(const erhe::scene::Node& node) -> bool
{
    for (const Dependency_property* const property : Ik::all_properties()) {
        if (node.has_local_value(*property)) {
            return true;
        }
    }
    return false;
}

} // namespace editor
