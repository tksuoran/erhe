#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace erhe::scene {

class Xformable; using Node = Xformable;

class Skin_data
{
public:
    // World-from-bind matrix for joint i - the matrix GPU skinning poses
    // vertices with (Joint_buffer): world_from_joint * inverse_bind. Falls back
    // to an identity inverse-bind when the skin carries fewer inverse bind
    // matrices than joints. Returns nullopt when the joint index is out of
    // range or the joint node is missing.
    [[nodiscard]] auto get_world_from_bind(std::size_t joint_index) const -> std::optional<glm::mat4>;

    uint32_t                                        joint_buffer_index{0}; // updated by Joint_buffer::update()
    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    std::vector<glm::mat4>                          inverse_bind_matrices;
    std::shared_ptr<erhe::scene::Node>              skeleton;
};

class Skin : public Item<Item_base, erhe::Typed, Skin>
{
public:
    Skin();
    explicit Skin(const Skin&);
    Skin& operator=(const Skin&);
    ~Skin() noexcept override;

    explicit Skin(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Skin"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | Item_type::skin; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/erhe/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Skin"; }

    // Computed (doc/erhe/property_system.md D26): the skeleton node's name
    // and the joint count, in the "Skin" group.
    static const erhe::property::Property<std::string> skeleton_property;
    static const erhe::property::Property<int>         joint_count_property;

    Skin_data skin_data;
};

[[nodiscard]] auto operator<(const Skin& lhs, const Skin& rhs) -> bool;

// The node an editor should transform in order to move a skinned mesh.
//
// Skinning ignores the mesh node's own transform entirely (glTF 2.0: "Only the
// joint transforms are applied to the skinned mesh; the transform of the
// skinned mesh node MUST be ignored"), so moving the host node has no visible
// effect. Moving any node that is an ancestor of every joint does move the
// skinned result rigidly - that node is what this returns.
//
// Uses Skin_data::skeleton when set (glTF's optional pivot-point hint), and
// otherwise computes the closest common ancestor of Skin_data::joints. Returns
// nullptr when there are no joints, or when the joints have no common ancestor
// (they belong to different trees - malformed, but not worth asserting on).
//
// Not cached: the walk is O(joints * depth) and callers are selection / gizmo
// updates, not per-primitive render code.
[[nodiscard]] auto get_skin_transform_root(const Skin& skin) -> std::shared_ptr<Node>;

// The node's local rotation in the bind pose, when the node and its parent
// node are joints of the same skin: the orthonormalized rotation of
// inverse(world_from_bind(parent)) * world_from_bind(joint), using the first
// such skin in Scene::get_skins() order so the answer is deterministic for a
// node that several skins list. Returns nullopt when the node has no parent
// node, belongs to no scene, or no skin lists both the node and its parent.
//
// It is what a rest orientation of a joint is taken from (the IK limits'
// zero angle, doc/plans/rigging/ik_settings.md section 1).
[[nodiscard]] auto get_bind_pose_local_rotation(const Node& node) -> std::optional<glm::quat>;

[[nodiscard]] auto is_bone(const Item_base* const item) -> bool;
[[nodiscard]] auto is_bone(const std::shared_ptr<Item_base>& item) -> bool;

// Set Item_flags::bone on every node the skin lists as a joint, so a joint is
// identifiable without walking every skin. Idempotent; call when a skin enters
// a scene. Joint-ness is a per-instance flag rather than an Item_type because a
// joint is an ordinary Node (see is_bone).
void mark_skin_joints(const Skin& skin);

} // namespace erhe::scene
