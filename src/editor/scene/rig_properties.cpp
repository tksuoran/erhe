#include "scene/rig_properties.hpp"

#include "rig/bone_tail.hpp"

#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node_system.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <optional>
#include <string>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr std::string_view c_rig_group = "Rig";

[[nodiscard]] auto is_bone_node(const Dependency_object& object) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    return (node != nullptr) && erhe::scene::is_bone(node);
}

// R2: the rest transform defaults to the bind pose the skin's inverse bind
// matrices encode, identity when no skin lists the node. The object is not
// always a Node - a Style holds every class's properties (D30) - so the cast
// is checked.
[[nodiscard]] auto get_bind_pose_trs(const Dependency_object& object) -> erhe::scene::Trs_transform
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    if (node != nullptr) {
        const std::optional<glm::mat4> bind_pose = erhe::scene::get_bind_pose_parent_from_node(*node);
        if (bind_pose.has_value()) {
            return erhe::scene::Trs_transform{bind_pose.value()};
        }
    }
    return erhe::scene::Trs_transform{};
}

[[nodiscard]] auto compute_rest_translation(const Dependency_object& object) -> Property_value
{
    return get_bind_pose_trs(object).get_translation();
}

[[nodiscard]] auto compute_rest_rotation(const Dependency_object& object) -> Property_value
{
    return get_bind_pose_trs(object).get_rotation();
}

[[nodiscard]] auto compute_rest_scale(const Dependency_object& object) -> Property_value
{
    return get_bind_pose_trs(object).get_scale();
}

[[nodiscard]] auto compute_tail(const Dependency_object& object) -> Property_value
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    if (node == nullptr) {
        return glm::vec3{0.0f, 1.0f, 0.0f};
    }
    return compute_default_bone_tail(*node);
}

// R9: a bone a skin lists keeps the tail and the rest pose its bind implies;
// the inverse bind matrices and weights would go stale (editing a bound
// skeleton is Phase 6). Clearing the value (back to the default) is not a
// write and stays allowed.
[[nodiscard]] auto validate_unbound(const Dependency_object& object, const char* const what, std::string& out_error) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    if (node == nullptr) {
        return true;
    }
    const std::optional<erhe::scene::Skin_joint> skin_joint = erhe::scene::find_skin_joint(*node);
    if (!skin_joint.has_value()) {
        return true;
    }
    out_error =
        "'" + node->get_name() + "' is a joint of skin '" + skin_joint.value().skin->get_name() +
        "': the " + what + " of a bound bone is fixed by its bind (skeleton_editing.md R9)";
    return false;
}

[[nodiscard]] auto validate_tail(const Dependency_object& object, const Property_value&, std::string& out_error) -> bool
{
    return validate_unbound(object, "tail", out_error);
}

[[nodiscard]] auto validate_rest(const Dependency_object& object, const Property_value&, std::string& out_error) -> bool
{
    return validate_unbound(object, "rest transform", out_error);
}

// Rig.display_color is listed only while the bone draws in its own color.
[[nodiscard]] auto is_custom_colored_bone(const Dependency_object& object) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    return (node != nullptr) && erhe::scene::is_bone(node) &&
        (node->get_value(Rig::display_color_mode_property()) == Bone_color_mode::custom);
}

constexpr erhe::property::Enum_entry c_bone_display_shape_entries[] = {
    { "octahedral", static_cast<int32_t>(Bone_display_shape::octahedral) },
    { "stick",      static_cast<int32_t>(Bone_display_shape::stick)      },
    { "box",        static_cast<int32_t>(Bone_display_shape::box)        }
};

constexpr erhe::property::Enum_entry c_bone_color_mode_entries[] = {
    { "style",  static_cast<int32_t>(Bone_color_mode::style)  },
    { "custom", static_cast<int32_t>(Bone_color_mode::custom) }
};

} // anonymous namespace

const erhe::property::Enum_info c_bone_display_shape_enum_info{"Bone_display_shape", c_bone_display_shape_entries};
const erhe::property::Enum_info c_bone_color_mode_enum_info   {"Bone_color_mode",    c_bone_color_mode_entries};

auto c_str(const Bone_display_shape shape) -> const char*
{
    switch (shape) {
        case Bone_display_shape::octahedral: return "octahedral";
        case Bone_display_shape::stick:      return "stick";
        case Bone_display_shape::box:        return "box";
        default:                             return "?";
    }
}

auto c_str(const Bone_color_mode mode) -> const char*
{
    switch (mode) {
        case Bone_color_mode::style:  return "style";
        case Bone_color_mode::custom: return "custom";
        default:                      return "?";
    }
}

auto Rig::property_owner_type() -> erhe::property::Owner_type
{
    // As Ik: no Item<> allocates the id, so it sits directly under the root
    // and serves only to qualify the names (Rig.rest_rotation).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Rig"
    );
    return s_id;
}

auto Rig::rest_translation_property() -> const Property<glm::vec3>&
{
    static const Property<glm::vec3> s_property = Property<glm::vec3>::register_attached(
        "rest_translation", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value   = glm::vec3{0.0f},
            .ui              = Property_ui{.group = c_rig_group, .tooltip = "Local translation of the bone's rest pose (what Clear Location restores); unset, the bind-pose translation. Refused on a bone a skin lists", .label = "Rest Translation", .visible_when = is_bone_node},
            .bridge          = erhe::property::Property_bridge{.validate = validate_rest},
            .compute_default = compute_rest_translation
        }
    );
    return s_property;
}

auto Rig::rest_rotation_property() -> const Property<glm::quat>&
{
    static const Property<glm::quat> s_property = Property<glm::quat>::register_attached(
        "rest_rotation", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value   = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
            .ui              = Property_ui{.group = c_rig_group, .tooltip = "Local rotation of the bone's rest pose (what Clear Rotation restores, and the default zero of the IK limits); unset, the bind-pose rotation. Refused on a bone a skin lists", .label = "Rest Rotation", .visible_when = is_bone_node},
            .bridge          = erhe::property::Property_bridge{.validate = validate_rest},
            .compute_default = compute_rest_rotation
        }
    );
    return s_property;
}

auto Rig::rest_scale_property() -> const Property<glm::vec3>&
{
    static const Property<glm::vec3> s_property = Property<glm::vec3>::register_attached(
        "rest_scale", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value   = glm::vec3{1.0f},
            .ui              = Property_ui{.group = c_rig_group, .tooltip = "Local scale of the bone's rest pose (what Clear Scale restores); unset, the bind-pose scale. Refused on a bone a skin lists", .label = "Rest Scale", .visible_when = is_bone_node},
            .bridge          = erhe::property::Property_bridge{.validate = validate_rest},
            .compute_default = compute_rest_scale
        }
    );
    return s_property;
}

auto Rig::tail_property() -> const Property<glm::vec3>&
{
    static const Property<glm::vec3> s_property = Property<glm::vec3>::register_attached(
        "tail", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value    = glm::vec3{0.0f, 1.0f, 0.0f},
            .property_changed = erhe::scene::node_system_property_changed,
            .ui               = Property_ui{.group = c_rig_group, .tooltip = "Head-to-tail vector of the bone in its local frame; moves the connected child bones with it. Unset: the first child bone's head (skinned joints: inferred from the skin), else the parent's bone length along +Y. Refused on a bone a skin lists", .label = "Tail", .visible_when = is_bone_node},
            .bridge           = erhe::property::Property_bridge{.validate = validate_tail},
            .compute_default  = compute_tail
        }
    );
    return s_property;
}

auto Rig::connected_property() -> const Property<bool>&
{
    static const Property<bool> s_property = Property<bool>::register_attached(
        "connected", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value = false,
            .ui            = Property_ui{.group = c_rig_group, .tooltip = "The bone's head stays on its parent bone's tail: setting it moves the bone there, and editing the parent's tail moves the bone with it", .label = "Connected", .visible_when = is_bone_node}
        }
    );
    return s_property;
}

auto Rig::display_color_mode_property() -> const Property<Bone_color_mode>&
{
    static const Property<Bone_color_mode> s_property = Property<Bone_color_mode>::register_attached(
        "display_color_mode", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(), c_bone_color_mode_enum_info,
        Property_metadata{
            .default_value    = erhe::property::make_value(Bone_color_mode::style),
            .property_changed = erhe::scene::node_system_property_changed,
            .ui               = Property_ui{.group = c_rig_group, .tooltip = "'style': the bone draws in the editor's bone colors; 'custom': in its own Display Color. Selected and hovered bones use the selection / hover colors either way", .label = "Display Color Mode", .visible_when = is_bone_node}
        }
    );
    return s_property;
}

auto Rig::display_color_property() -> const Property<glm::vec3>&
{
    static const Property<glm::vec3> s_property = Property<glm::vec3>::register_attached(
        "display_color", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(),
        Property_metadata{
            .default_value    = glm::vec3{1.0f, 0.45f, 0.0f},
            .property_changed = erhe::scene::node_system_property_changed,
            .ui               = Property_ui{.presentation = Property_ui::Presentation::color, .group = c_rig_group, .tooltip = "The bone's own color, in the solid (shaded) and the line style", .label = "Display Color", .visible_when = is_custom_colored_bone}
        }
    );
    return s_property;
}

auto Rig::display_shape_property() -> const Property<Bone_display_shape>&
{
    static const Property<Bone_display_shape> s_property = Property<Bone_display_shape>::register_attached(
        "display_shape", Rig::property_owner_type(), erhe::scene::Node::property_owner_type(), c_bone_display_shape_enum_info,
        Property_metadata{
            .default_value    = erhe::property::make_value(Bone_display_shape::octahedral),
            .property_changed = erhe::scene::node_system_property_changed,
            .ui               = Property_ui{.group = c_rig_group, .tooltip = "The shape the bone is drawn as: octahedral, stick (thin) or box", .label = "Display Shape", .visible_when = is_bone_node}
        }
    );
    return s_property;
}

// Registered at static initialization like every other property, so the
// registry lists them (and finds them by qualified name, as a scene load
// does) before anything else runs; the accessors' own statics keep the order
// safe for a registration in another translation unit that names them.
namespace {
[[maybe_unused]] const Property<glm::vec3>& rest_translation_registration = Rig::rest_translation_property();
[[maybe_unused]] const Property<glm::quat>& rest_rotation_registration    = Rig::rest_rotation_property();
[[maybe_unused]] const Property<glm::vec3>& rest_scale_registration       = Rig::rest_scale_property();
[[maybe_unused]] const Property<glm::vec3>& tail_registration             = Rig::tail_property();
[[maybe_unused]] const Property<bool>&      connected_registration        = Rig::connected_property();
[[maybe_unused]] const Property<Bone_color_mode>&    display_color_mode_registration = Rig::display_color_mode_property();
[[maybe_unused]] const Property<glm::vec3>&          display_color_registration      = Rig::display_color_property();
[[maybe_unused]] const Property<Bone_display_shape>& display_shape_registration      = Rig::display_shape_property();
} // anonymous namespace

auto Rig::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        rest_translation_property().get_ptr(),
        rest_rotation_property   ().get_ptr(),
        rest_scale_property      ().get_ptr(),
        tail_property            ().get_ptr(),
        connected_property       ().get_ptr(),
        display_color_mode_property().get_ptr(),
        display_color_property   ().get_ptr(),
        display_shape_property   ().get_ptr()
    };
    return s_properties;
}

auto read_rest_transform(const erhe::scene::Node& node) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{
        node.get_value(Rig::rest_translation_property()),
        node.get_value(Rig::rest_rotation_property()),
        node.get_value(Rig::rest_scale_property())
    };
}

auto has_local_rig_value(const erhe::scene::Node& node) -> bool
{
    for (const Dependency_property* const property : Rig::all_properties()) {
        if (node.has_local_value(*property)) {
            return true;
        }
    }
    return false;
}

auto get_bone_display_color(const erhe::scene::Node& node) -> std::optional<glm::vec3>
{
    if (node.get_value(Rig::display_color_mode_property()) != Bone_color_mode::custom) {
        return std::nullopt;
    }
    return node.get_value(Rig::display_color_property());
}

} // namespace editor
