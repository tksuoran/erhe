#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <vector>

namespace erhe::scene {
    class Trs_transform;
    class Xformable; using Node = Xformable;
}

namespace editor {

// R17 (doc/plans/rigging/skeleton_editing.md): the shape a bone is drawn as,
// in the solid style (the pickable bone proxy) and in the line style.
enum class Bone_display_shape : int {
    octahedral = 0, // the octahedron widest at a tenth of the bone
    stick      = 1, // a thin square prism, a quarter of the octahedron's width
    box        = 2  // a square prism as wide as the octahedron's ring
};

// R17: where a bone's display color comes from. `style` is the editor's
// Debug_visualizations_style colors (the solid style's N.V grey, the line
// style's alternating skin_bone_color_a / b); `custom` is the bone's own
// Rig.display_color. Selected and hovered bones draw in the style's
// selected / hover colors either way.
enum class Bone_color_mode : int {
    style  = 0,
    custom = 1
};

[[nodiscard]] auto c_str(Bone_display_shape shape) -> const char*;
[[nodiscard]] auto c_str(Bone_color_mode mode) -> const char*;

extern const erhe::property::Enum_info c_bone_display_shape_enum_info;
extern const erhe::property::Enum_info c_bone_color_mode_enum_info;

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
    // identity channel when no skin lists the node. A write on a bone a skin
    // lists is refused (R9, the bridge's validate names the skin): its rest
    // is its bind pose. The bone structure verbs (rig/bone_structure.hpp)
    // record the creation local TRS of the bones they create.
    //
    // Accessors registering on first use rather than static members: another
    // translation unit's static registration (Ik.rest_rotation, whose
    // default_from is rest_rotation) needs them registered first, and the
    // order of static initialization across translation units is unspecified.
    [[nodiscard]] static auto rest_translation_property() -> const erhe::property::Property<glm::vec3>&;
    [[nodiscard]] static auto rest_rotation_property   () -> const erhe::property::Property<glm::quat>&;
    [[nodiscard]] static auto rest_scale_property      () -> const erhe::property::Property<glm::vec3>&;

    // R3: the bone's tail, head (the node origin) to tail in the bone's local
    // frame. Per-object default (D31): compute_default_bone_tail
    // (rig/bone_tail.hpp) - the skinned inference for a joint a skin lists,
    // else the first bone child's head, else the parent's bone length along
    // +Y. A write on a bone a skin lists is refused (R9, the bridge's
    // validate names the skin): its tail belongs to the bind. A change
    // reaches the scene's node systems (node_system_property_changed), which
    // is how the bone display learns of it.
    [[nodiscard]] static auto tail_property     () -> const erhe::property::Property<glm::vec3>&;
    // R4: the bone's head stays on its parent's tail. Setting it snaps the
    // node's local translation to the parent's Rig.tail and a parent's tail
    // edit moves its connected children; both are follow-ups the edit's
    // Property_set_operation records in its own undo step
    // (rig/bone_connect.hpp).
    [[nodiscard]] static auto connected_property() -> const erhe::property::Property<bool>&;

    // R17: how the bone is drawn. Display only - nothing about the bind or
    // the pose reads them - so, unlike Rig.tail and Rig.rest_*, a bone a
    // skin lists accepts them. A change reaches the scene's node systems
    // (node_system_property_changed): Rig_system reports it to the bone
    // display, which re-materials / reshapes that bone's proxy. The line
    // style reads them where it draws the bone.
    //   Rig.display_color_mode: Bone_color_mode, default `style`.
    //   Rig.display_color:      vec3 color presentation, the color of a
    //                           `custom` bone (listed only then); default
    //                           orange.
    //   Rig.display_shape:      Bone_display_shape, default `octahedral`.
    [[nodiscard]] static auto display_color_mode_property() -> const erhe::property::Property<Bone_color_mode>&;
    [[nodiscard]] static auto display_color_property     () -> const erhe::property::Property<glm::vec3>&;
    [[nodiscard]] static auto display_shape_property     () -> const erhe::property::Property<Bone_display_shape>&;

    // Every Rig.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// The effective rest transform of a node: its Rig.rest_* values composed.
[[nodiscard]] auto read_rest_transform(const erhe::scene::Node& node) -> erhe::scene::Trs_transform;

// True when the node holds a local value of any Rig.* property.
[[nodiscard]] auto has_local_rig_value(const erhe::scene::Node& node) -> bool;

// R17: the bone's own display color when its Rig.display_color_mode is
// `custom`, nullopt when it draws in the style colors.
[[nodiscard]] auto get_bone_display_color(const erhe::scene::Node& node) -> std::optional<glm::vec3>;

} // namespace editor
