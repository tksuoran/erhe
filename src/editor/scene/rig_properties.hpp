#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace erhe::scene {
    class Trs_transform;
    class Xformable; using Node = Xformable;
}

namespace editor {

// Per-bone rig values as attached properties of the bone node itself
// (doc/plans/rigging/skeleton_editing.md section 1), owner type "Rig", UI
// group "Rig".
//
// The rest transform of R2 (named Rig.rest_transform in the plan) is three
// properties, one per TRS channel, because the property system has no TRS
// value type and the posing verbs (Clear Location / Rotation / Scale) read
// the channels separately: Rig.rest_translation (vec3), Rig.rest_rotation
// (quat) and Rig.rest_scale (vec3), together the bone's local
// (parent-from-node) transform at rest.
//
// Rig is a registration holder, not an item and not a Dependency_object, like
// editor::Ik: the holder of every value is an erhe::scene::Node.
class Rig
{
public:
    Rig() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // Attached to erhe::scene::Node, UI group "Rig", not inheriting, listed on
    // bones. Per-object default (D31): the channel of the node's bind-pose
    // local transform (erhe::scene::get_bind_pose_parent_from_node), the
    // identity channel when no skin lists the node.
    //
    // Accessors registering on first use rather than static members: another
    // translation unit's static registration (Ik.rest_rotation, whose
    // default_from is rest_rotation) needs them registered first, and the
    // order of static initialization across translation units is unspecified.
    [[nodiscard]] static auto rest_translation_property() -> const erhe::property::Property<glm::vec3>&;
    [[nodiscard]] static auto rest_rotation_property   () -> const erhe::property::Property<glm::quat>&;
    [[nodiscard]] static auto rest_scale_property      () -> const erhe::property::Property<glm::vec3>&;

    // Every Rig.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// The effective rest transform of a node: its Rig.rest_* values composed.
[[nodiscard]] auto read_rest_transform(const erhe::scene::Node& node) -> erhe::scene::Trs_transform;

// True when the node holds a local value of any Rig.* property.
[[nodiscard]] auto has_local_rig_value(const erhe::scene::Node& node) -> bool;

} // namespace editor
